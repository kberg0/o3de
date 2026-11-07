/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#include "MultiplayerScriptDefinedAuthoring.h"
#include <AzCore/IO/Path/Path.h>
#include <AzCore/IO/SystemFile.h>
#include <AzCore/JSON/document.h>
#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/writer.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Script/ScriptContextAttributes.h>
#include <AzCore/Serialization/Json/JsonSerialization.h>
#include <AzCore/Serialization/Utils.h>
#include <AzCore/Utils/Utils.h>
#include <Multiplayer/MultiplayerScriptDefinedDefinition.h>
#include <limits>

namespace ScriptCanvasMultiplayer
{
    using namespace Multiplayer;
    namespace
    {
        using Result = ScriptDefinedAuthoringResult;
        Result Fail(const char* message)
        {
            return AZ::Failure(AZStd::string(message));
        }

        AZ::Outcome<AZ::IO::Path, AZStd::string> SourcePath(const AZStd::string& requested, const char* extension)
        {
            AZ::IO::Path root(AZ::Utils::GetProjectPath());
            AZ::IO::Path path = (root / requested).LexicallyNormal();
            if (requested.find('\0') != AZStd::string::npos || root.empty() || !path.IsRelativeTo(root) || path.Extension() != extension)
            {
                return AZ::Failure(AZStd::string("Source must have the expected extension and remain inside the current project"));
            }
            return AZ::Success(AZStd::move(path));
        }

        AZStd::string JsonText(const rapidjson::Value& value)
        {
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            value.Accept(writer);
            return AZStd::string(buffer.GetString(), buffer.GetSize());
        }

        bool JsonLoaded(const AZ::JsonSerializationResult::ResultCode& result)
        {
            using Outcome = AZ::JsonSerializationResult::Outcomes;
            return result.GetOutcome() == Outcome::Success || result.GetOutcome() == Outcome::DefaultsUsed ||
                result.GetOutcome() == Outcome::PartialDefaults;
        }

        template<class Save>
        Result WriteSource(const AZ::IO::Path& path, bool overwrite, Save save)
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
            const AZ::IO::Path temporary(path.Native() + "." + AZ::Uuid::CreateRandom().ToFixedString(false, false).c_str() + ".tmp");
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
        auto path = SourcePath(requested, ".mpschema");
        if (!path)
        {
            return AZ::Failure(path.TakeError());
        }
        AZStd::unique_ptr<MultiplayerScriptDefinedAsset> asset(
            AZ::Utils::LoadObjectFromFile<MultiplayerScriptDefinedAsset>(path.GetValue().Native()));
        if (!asset)
        {
            return Fail("Cannot load schema source");
        }
        rapidjson::Document document;
        AZ::JsonSerializerSettings settings;
        settings.m_keepDefaults = true;
        const auto result = AZ::JsonSerialization::Store(document, document.GetAllocator(), asset->m_definition, settings);
        return JsonLoaded(result) ? AZ::Success(JsonText(document)) : Fail("Cannot serialize schema definition to JSON");
    }

    ScriptDefinedAuthoringResult SaveScriptDefinedSchema(const AZStd::string& requested, const AZStd::string& json, bool overwrite)
    {
        auto path = SourcePath(requested, ".mpschema");
        if (!path)
        {
            return AZ::Failure(path.TakeError());
        }
        rapidjson::Document document;
        document.Parse(json.c_str());
        if (document.HasParseError() || !document.IsObject())
        {
            return Fail("Definition must be a JSON object");
        }
        MultiplayerScriptDefinedAsset asset;
        const auto loaded = AZ::JsonSerialization::Load(asset.m_definition, document);
        if (!JsonLoaded(loaded))
        {
            return AZ::Failure(AZStd::string("Invalid serialized definition JSON: ") + loaded.ToString(""));
        }
        if (auto validation = asset.m_definition.Validate(); !validation)
        {
            return AZ::Failure(validation.TakeError());
        }
        if (AZ::IO::SystemFile::Exists(path.GetValue().c_str()))
        {
            AZStd::unique_ptr<MultiplayerScriptDefinedAsset> previous(
                AZ::Utils::LoadObjectFromFile<MultiplayerScriptDefinedAsset>(path.GetValue().Native()));
            if (!previous || previous->m_definition.m_revision == std::numeric_limits<AZ::u32>::max())
            {
                return Fail("Cannot revise existing schema");
            }
            asset.m_definition.m_revision = previous->m_definition.m_revision + 1;
        }
        return WriteSource(
            path.GetValue(),
            overwrite,
            [&](const AZ::IO::Path& temporary)
            {
                return AZ::Utils::SaveObjectToFile(temporary.Native(), AZ::ObjectStream::ST_XML, &asset);
            });
    }

    void ReflectScriptDefinedAuthoring(AZ::ReflectContext* context)
    {
        if (auto* behavior = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behavior->Method("ScriptDefined_SaveSchema", &SaveScriptDefinedSchema)
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation)
                ->Attribute(AZ::Script::Attributes::Module, "multiplayer.authoring");
            behavior->Method("ScriptDefined_ReadSchema", &ReadScriptDefinedSchema)
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation)
                ->Attribute(AZ::Script::Attributes::Module, "multiplayer.authoring");
        }
    }
} // namespace ScriptCanvasMultiplayer
