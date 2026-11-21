/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "MultiplayerScriptDefinedAuthoring.h"
#include <AzCore/IO/Path/Path.h>
#include <AzCore/IO/SystemFile.h>
#include <AzCore/JSON/document.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Script/ScriptContextAttributes.h>
#include <AzCore/Serialization/Json/JsonSerialization.h>
#include <AzCore/Serialization/Json/JsonUtils.h>
#include <AzCore/Serialization/Utils.h>
#include <AzCore/Utils/Utils.h>
#include <Multiplayer/MultiplayerScriptDefinedDefinition.h>
#include <limits>

namespace ScriptCanvasMultiplayer
{
    namespace
    {
        //! Behavior context module of the authoring functions.
        constexpr const char* AuthoringModule = "multiplayer.authoring";

        ScriptDefinedAuthoringResult Fail(const char* message)
        {
            return AZ::Failure(AZStd::string(message));
        }

        //! Resolves a project-relative source path. Fails if the path leaves the project or has another extension.
        AZ::Outcome<AZ::IO::Path, AZStd::string> ResolveSourcePath(const AZStd::string& requested, const char* extension)
        {
            const AZ::IO::Path projectRoot(AZ::Utils::GetProjectPath());
            AZ::IO::Path path = (projectRoot / requested).LexicallyNormal();

            const bool hasEmbeddedNull = requested.find('\0') != AZStd::string::npos;
            if (hasEmbeddedNull || projectRoot.empty() || !path.IsRelativeTo(projectRoot) || path.Extension() != extension)
            {
                return AZ::Failure(AZStd::string("Source must have the expected extension and remain inside the current project"));
            }
            return AZ::Success(AZStd::move(path));
        }

        //! Returns true if JSON serialization finished, including when it filled in defaults.
        bool IsJsonSerialized(const AZ::JsonSerializationResult::ResultCode& result)
        {
            using AZ::JsonSerializationResult::Outcomes;
            const Outcomes outcome = result.GetOutcome();
            return outcome == Outcomes::Success || outcome == Outcomes::DefaultsUsed || outcome == Outcomes::PartialDefaults;
        }

        //! Writes a source through a temporary file and renames it into place, so readers never see a partial file.
        template<class SaveFunction>
        ScriptDefinedAuthoringResult WriteSourceAtomically(const AZ::IO::Path& path, bool overwrite, SaveFunction save)
        {
            if (!overwrite && AZ::IO::SystemFile::Exists(path.c_str()))
            {
                return Fail("Source already exists; overwrite was not requested");
            }

            const AZ::IO::Path parent(path.ParentPath());
            if (!AZ::IO::SystemFile::CreateDir(parent.c_str()))
            {
                return Fail("Cannot create source directory");
            }

            const AZStd::string uniqueSuffix =
                AZStd::string::format(".%s.tmp", AZ::Uuid::CreateRandom().ToFixedString(false, false).c_str());
            const AZ::IO::Path temporary(path.Native() + uniqueSuffix);
            if (!save(temporary) || !AZ::IO::SystemFile::Rename(temporary.c_str(), path.c_str(), overwrite))
            {
                AZ::IO::SystemFile::Delete(temporary.c_str());
                return Fail("Cannot save source atomically");
            }
            return AZ::Success(AZStd::string(path.Native()));
        }
    } // namespace

    ScriptDefinedAuthoringResult ReadScriptDefinedSchema(const AZStd::string& requested)
    {
        auto path = ResolveSourcePath(requested, ScriptDefinedSchemaDotExtension);
        if (!path)
        {
            return AZ::Failure(path.TakeError());
        }

        AZStd::unique_ptr<Multiplayer::MultiplayerScriptDefinedAsset> asset(
            AZ::Utils::LoadObjectFromFile<Multiplayer::MultiplayerScriptDefinedAsset>(path.GetValue().Native()));
        if (!asset)
        {
            return Fail("Cannot load schema source");
        }

        // Keep defaults so that callers see every field, including the ones they did not set.
        rapidjson::Document document;
        AZ::JsonSerializerSettings settings;
        settings.m_keepDefaults = true;
        const auto stored = AZ::JsonSerialization::Store(document, document.GetAllocator(), asset->GetDefinition(), settings);
        if (!IsJsonSerialized(stored))
        {
            return Fail("Cannot serialize schema definition to JSON");
        }

        AZStd::string json;
        if (!AZ::JsonSerializationUtils::WriteJsonString(document, json))
        {
            return Fail("Cannot serialize schema definition to JSON");
        }
        return AZ::Success(AZStd::move(json));
    }

    ScriptDefinedAuthoringResult SaveScriptDefinedSchema(const AZStd::string& requested, const AZStd::string& json, bool overwrite)
    {
        auto path = ResolveSourcePath(requested, ScriptDefinedSchemaDotExtension);
        if (!path)
        {
            return AZ::Failure(path.TakeError());
        }

        auto document = AZ::JsonSerializationUtils::ReadJsonString(json);
        if (!document || !document.GetValue().IsObject())
        {
            return Fail("Definition must be a JSON object");
        }

        Multiplayer::MultiplayerScriptDefinedAsset asset;
        const auto loaded = AZ::JsonSerialization::Load(asset.ModifyDefinition(), document.GetValue());
        if (!IsJsonSerialized(loaded))
        {
            return AZ::Failure(AZStd::string("Invalid serialized definition JSON: ") + loaded.ToString(""));
        }

        if (auto validation = asset.GetDefinition().Validate(); !validation)
        {
            return AZ::Failure(validation.TakeError());
        }

        // Saving over an existing source is a new revision of it.
        if (AZ::IO::SystemFile::Exists(path.GetValue().c_str()))
        {
            AZStd::unique_ptr<Multiplayer::MultiplayerScriptDefinedAsset> previous(
                AZ::Utils::LoadObjectFromFile<Multiplayer::MultiplayerScriptDefinedAsset>(path.GetValue().Native()));
            if (!previous || previous->GetDefinition().m_revision == std::numeric_limits<AZ::u32>::max())
            {
                return Fail("Cannot revise existing schema");
            }
            asset.ModifyDefinition().m_revision = previous->GetDefinition().m_revision + 1;
        }

        return WriteSourceAtomically(
            path.GetValue(),
            overwrite,
            [&asset](const AZ::IO::Path& temporary)
            {
                return AZ::Utils::SaveObjectToFile(temporary.Native(), AZ::ObjectStream::ST_XML, &asset);
            });
    }

    void ReflectScriptDefinedAuthoring(AZ::ReflectContext* context)
    {
        if (auto* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->Method("ScriptDefined_SaveSchema", &SaveScriptDefinedSchema)
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation)
                ->Attribute(AZ::Script::Attributes::Module, AuthoringModule);
            behaviorContext->Method("ScriptDefined_ReadSchema", &ReadScriptDefinedSchema)
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation)
                ->Attribute(AZ::Script::Attributes::Module, AuthoringModule);
        }
    }
} // namespace ScriptCanvasMultiplayer
