/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "MultiplayerScriptDefinedEditor.h"
#include "MultiplayerScriptDefinedAuthoring.h"
#include <AzCore/IO/Path/Path.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/Utils.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <AzToolsFramework/ToolsComponents/ToolsAssetCatalogBus.h>
#include <Multiplayer/Components/MultiplayerScriptDefinedComponent.h>
#include <Multiplayer/Components/MultiplayerScriptDefinedInputComponent.h>
#include <MultiplayerScriptDefinedBridge.h>
#include <MultiplayerScriptDefinedInterface.h>
#include <limits>

namespace ScriptCanvasMultiplayer
{
    using namespace Multiplayer;

    namespace
    {
        //! Version of the schema builder. Increase it to rebuild every schema product.
        constexpr int SchemaBuilderVersion = 1;

        //! Sub ID of the compiled schema product.
        constexpr AZ::u32 SchemaProductSubId = 0;

        //! Extensions of the two products, appended to the source file name.
        constexpr const char* SchemaProductExtension = ".compiled";
        constexpr const char* InterfaceProductExtension = ".scriptevents_compiled";

        //! Service provided by the runtime script-defined component, which this editor component exports.
        constexpr AZ::Crc32 ScriptDefinedComponentService = AZ_CRC_CE("MultiplayerScriptDefinedComponent");
    } // namespace

    MultiplayerScriptDefinedAssetHandler::MultiplayerScriptDefinedAssetHandler()
        : GenericAssetHandler(
              "Multiplayer Script Definition",
              "Multiplayer",
              ScriptDefinedSchemaExtension,
              AZ::Uuid::CreateNull(),
              nullptr,
              AZ::ObjectStream::ST_XML,
              false)
    {
    }

    bool MultiplayerScriptDefinedAssetHandler::SaveAssetData(
        const AZ::Data::Asset<AZ::Data::AssetData>& asset, AZ::IO::GenericStream* stream)
    {
        auto* schema = asset.GetAs<MultiplayerScriptDefinedAsset>();
        if (!schema || schema->GetDefinition().m_revision == std::numeric_limits<AZ::u32>::max())
        {
            return false;
        }

        schema->ValidateForEditor();
        const auto validation = schema->GetDefinition().Validate();
        if (!validation)
        {
            AZ_Error("Multiplayer", false, "Cannot save schema: %s", validation.GetError().c_str());
            return false;
        }

        // Script Events chooses the highest registered version, so authored revisions must increase.
        // Restore the revision if the save fails.
        ++schema->ModifyDefinition().m_revision;
        if (GenericAssetHandler::SaveAssetData(asset, stream))
        {
            return true;
        }
        --schema->ModifyDefinition().m_revision;
        return false;
    }

    void ScriptDefinedArchetypeOverride::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ScriptDefinedArchetypeOverride>()
                ->Version(1)
                ->Field("Id", &ScriptDefinedArchetypeOverride::m_id)
                ->Field("Name", &ScriptDefinedArchetypeOverride::m_name)
                ->Field("Value", &ScriptDefinedArchetypeOverride::m_value)
                ->Field("Override", &ScriptDefinedArchetypeOverride::m_override);

            if (AZ::EditContext* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ScriptDefinedArchetypeOverride>("Archetype value", "Prefab-local configuration")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::NameLabelOverride, &ScriptDefinedArchetypeOverride::GetLabel)
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ScriptDefinedArchetypeOverride::m_override, "Override default", "")
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &ScriptDefinedArchetypeOverride::m_value,
                        "Value",
                        "Same format as the schema's typed default");
            }
        }
    }

    void EditorMultiplayerScriptDefinedComponent::Reflect(AZ::ReflectContext* context)
    {
        MultiplayerScriptDefinedAsset::Reflect(context);
        ScriptDefinedArchetypeOverride::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<EditorMultiplayerScriptDefinedComponent, AzToolsFramework::Components::EditorComponentBase>()
                ->Version(1)
                ->Field("Definition", &EditorMultiplayerScriptDefinedComponent::m_asset)
                ->Field("Archetypes", &EditorMultiplayerScriptDefinedComponent::m_archetypes)
                ->Field("Status", &EditorMultiplayerScriptDefinedComponent::m_status);

            if (AZ::EditContext* editContext = serializeContext->GetEditContext())
            {
                editContext
                    ->Class<EditorMultiplayerScriptDefinedComponent>(
                        "Multiplayer Script Defined", "Define properties, inputs, archetypes and RPCs in a schema asset")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Multiplayer")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &EditorMultiplayerScriptDefinedComponent::m_asset,
                        "Definition",
                        "Create/edit a .mpschema in Asset Editor")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, &EditorMultiplayerScriptDefinedComponent::OnDefinitionChanged)
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &EditorMultiplayerScriptDefinedComponent::m_archetypes,
                        "Archetypes",
                        "Read-only runtime configuration; values are baked into the prefab")
                    ->Attribute(AZ::Edit::Attributes::ContainerCanBeModified, false)
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, &EditorMultiplayerScriptDefinedComponent::OnDefinitionChanged)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EditorMultiplayerScriptDefinedComponent::m_status, "Validation", "")
                    ->Attribute(AZ::Edit::Attributes::ReadOnly, true);
            }
        }
    }

    void EditorMultiplayerScriptDefinedComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(ScriptDefinedComponentService);
    }

    void EditorMultiplayerScriptDefinedComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(ScriptDefinedComponentService);
    }

    void EditorMultiplayerScriptDefinedComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("NetBindService"));
    }

    EditorMultiplayerScriptDefinedComponent::~EditorMultiplayerScriptDefinedComponent()
    {
        AZ::Data::AssetBus::Handler::BusDisconnect();
    }

    void EditorMultiplayerScriptDefinedComponent::Activate()
    {
        EditorComponentBase::Activate();
        OnDefinitionChanged();
    }

    void EditorMultiplayerScriptDefinedComponent::Deactivate()
    {
        AZ::Data::AssetBus::Handler::BusDisconnect();
        EditorComponentBase::Deactivate();
    }

    void EditorMultiplayerScriptDefinedComponent::SetDefinitionAsset(AZ::Data::Asset<MultiplayerScriptDefinedAsset> asset)
    {
        m_asset = AZStd::move(asset);
        OnDefinitionChanged();
    }

    AZ::u32 EditorMultiplayerScriptDefinedComponent::OnDefinitionChanged()
    {
        AZ::Data::AssetBus::Handler::BusDisconnect();
        if (m_asset.GetId().IsValid())
        {
            AZ::Data::AssetBus::Handler::BusConnect(m_asset.GetId());
            if (!m_asset.IsReady())
            {
                m_asset.QueueLoad();
            }
        }

        RefreshDefinition();
        return AZ::Edit::PropertyRefreshLevels::EntireTree;
    }

    void EditorMultiplayerScriptDefinedComponent::RefreshDefinition()
    {
        // Rebuild the archetype list from the schema, keeping the overrides of archetypes that still exist.
        if (m_asset.IsReady())
        {
            AZStd::vector<ScriptDefinedArchetypeOverride> updated;
            for (const auto& field : m_asset->GetDefinition().m_archetypes)
            {
                ScriptDefinedArchetypeOverride entry{ field.m_id, field.m_name, field.m_defaultValue, false };
                for (const auto& previous : m_archetypes)
                {
                    if (previous.m_id == field.m_id)
                    {
                        entry.m_override = previous.m_override;
                        if (entry.m_override)
                        {
                            entry.m_value = previous.m_value;
                        }
                        break;
                    }
                }
                updated.push_back(AZStd::move(entry));
            }
            m_archetypes = AZStd::move(updated);
        }

        const auto definition = ResolveDefinition();
        if (definition)
        {
            m_status = "Valid. Nodes appear under Multiplayer/" + definition.GetValue().m_name;
        }
        else
        {
            m_status = definition.GetError();
        }
    }

    void EditorMultiplayerScriptDefinedComponent::OnAssetReady(AZ::Data::Asset<AZ::Data::AssetData> asset)
    {
        m_asset = asset;
        RefreshDefinition();
        AzToolsFramework::ToolsApplicationEvents::Bus::Broadcast(
            &AzToolsFramework::ToolsApplicationEvents::InvalidatePropertyDisplay, AzToolsFramework::Refresh_EntireTree);
    }

    void EditorMultiplayerScriptDefinedComponent::OnAssetReloaded(AZ::Data::Asset<AZ::Data::AssetData> asset)
    {
        OnAssetReady(asset);
    }

    AZ::Outcome<MultiplayerScriptDefinedDefinition, AZStd::string> EditorMultiplayerScriptDefinedComponent::ResolveDefinition() const
    {
        if (!m_asset.IsReady())
        {
            return AZ::Failure(AZStd::string("Definition asset is not ready"));
        }

        MultiplayerScriptDefinedDefinition definition = m_asset->GetDefinition();

        // The builder uses the source UUID for both products, including copies of an authored asset.
        definition.m_id = m_asset.GetId().m_guid;

        for (const auto& entry : m_archetypes)
        {
            if (!entry.m_override)
            {
                continue;
            }

            bool found = false;
            for (auto& field : definition.m_archetypes)
            {
                if (field.m_id == entry.m_id)
                {
                    field.m_defaultValue = entry.m_value;
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                return AZ::Failure(AZStd::string("An overridden archetype no longer exists"));
            }
        }

        auto validation = definition.Validate();
        if (!validation)
        {
            return AZ::Failure(validation.TakeError());
        }
        return AZ::Success(AZStd::move(definition));
    }

    void EditorMultiplayerScriptDefinedComponent::BuildGameEntity(AZ::Entity* gameEntity)
    {
        // Prefab builders deserialize editor components without activating them.
        // QueueLoad therefore needs an explicit wait before exporting native state.
        if (m_asset.GetId().IsValid() && !m_asset.IsReady())
        {
            m_asset.QueueLoad();
            m_asset.BlockUntilLoadComplete();
        }

        const auto definition = ResolveDefinition();
        if (!definition)
        {
            AZ_Error("Multiplayer", false, "Invalid Script Defined component: %s", definition.GetError().c_str());
            return;
        }

        auto* component = gameEntity->CreateComponent<MultiplayerScriptDefinedComponent>();
        if (!component || !component->ConfigureDefinition(definition.GetValue()))
        {
            AZ_Error("Multiplayer", false, "Unable to export Script Defined component");
            return;
        }

        if (!definition.GetValue().m_inputs.empty())
        {
            auto* input = gameEntity->CreateComponent<MultiplayerScriptDefinedInputComponent>();
            if (!input || !input->ConfigureDefinition(definition.GetValue()))
            {
                AZ_Error("Multiplayer", false, "Unable to export Script Defined inputs");
                return;
            }
        }

        // The bridge binds the generated Script Events interface product of the same source.
        AZ::Data::Asset<ScriptEvents::ScriptEventsAsset> interfaceAsset(
            AZ::Data::AssetId(m_asset.GetId().m_guid, ScriptDefinedInterfaceSubId), azrtti_typeid<ScriptEvents::ScriptEventsAsset>());
        interfaceAsset.SetAutoLoadBehavior(AZ::Data::AssetLoadBehavior::PreLoad);
        gameEntity->CreateComponent<MultiplayerScriptDefinedBridge>()->Configure(AZStd::move(interfaceAsset));
    }

    void MultiplayerScriptDefinedBuilder::CreateJobs(
        const AssetBuilderSDK::CreateJobsRequest& request, AssetBuilderSDK::CreateJobsResponse& response) const
    {
        if (m_shutdown)
        {
            response.m_result = AssetBuilderSDK::CreateJobsResultCode::ShuttingDown;
            return;
        }

        for (const auto& platform : request.m_enabledPlatforms)
        {
            AssetBuilderSDK::JobDescriptor job;
            job.m_jobKey = ScriptEvents::k_builderJobKey;
            job.SetPlatformIdentifier(platform.m_identifier.c_str());
            job.m_critical = true;
            response.m_createJobOutputs.push_back(AZStd::move(job));
        }
        response.m_result = AssetBuilderSDK::CreateJobsResultCode::Success;
    }

    void MultiplayerScriptDefinedBuilder::ProcessJob(
        const AssetBuilderSDK::ProcessJobRequest& request, AssetBuilderSDK::ProcessJobResponse& response) const
    {
        if (m_shutdown)
        {
            response.m_resultCode = AssetBuilderSDK::ProcessJobResult_Cancelled;
            return;
        }

        AZStd::unique_ptr<MultiplayerScriptDefinedAsset> schema(
            AZ::Utils::LoadObjectFromFile<MultiplayerScriptDefinedAsset>(request.m_fullPath));
        if (!schema)
        {
            AZ_Error("Multiplayer", false, "Cannot load schema: %s", request.m_fullPath.c_str());
            return;
        }

        // The source UUID is the definition's ID, so copies of a source compile to distinct interfaces.
        schema->ModifyDefinition().m_id = request.m_sourceFileUUID;
        const auto schemaValidation = schema->GetDefinition().Validate();
        if (!schemaValidation)
        {
            AZ_Error("Multiplayer", false, "%s: %s", request.m_fullPath.c_str(), schemaValidation.GetError().c_str());
            return;
        }

        ScriptEvents::ScriptEventsAsset interfaceAsset;
        interfaceAsset.m_definition = MakeScriptDefinedInterface(schema->GetDefinition());
        const auto interfaceValidation = interfaceAsset.m_definition.Validate();
        if (!interfaceValidation)
        {
            AZ_Error("Multiplayer", false, "Invalid script interface: %s", interfaceValidation.GetError().c_str());
            return;
        }

        const AZ::IO::Path filename = AZ::IO::Path(request.m_sourceFile).Filename();
        const AZ::IO::Path schemaPath = AZ::IO::Path(request.m_tempDirPath) / (filename.Native() + SchemaProductExtension);
        const AZ::IO::Path interfacePath = AZ::IO::Path(request.m_tempDirPath) / (filename.Native() + InterfaceProductExtension);
        const bool saved = AZ::Utils::SaveObjectToFile(schemaPath.Native(), AZ::ObjectStream::ST_BINARY, schema.get()) &&
            AZ::Utils::SaveObjectToFile(interfacePath.Native(), AZ::ObjectStream::ST_BINARY, &interfaceAsset);
        if (!saved)
        {
            AZ_Error("Multiplayer", false, "Cannot save schema products");
            return;
        }

        AssetBuilderSDK::JobProduct schemaProduct(schemaPath.Native(), azrtti_typeid<MultiplayerScriptDefinedAsset>(), SchemaProductSubId);
        schemaProduct.m_dependenciesHandled = true;

        AssetBuilderSDK::JobProduct interfaceProduct(
            interfacePath.Native(), azrtti_typeid<ScriptEvents::ScriptEventsAsset>(), ScriptDefinedInterfaceSubId);
        interfaceProduct.m_dependenciesHandled = true;

        response.m_outputProducts.push_back(AZStd::move(schemaProduct));
        response.m_outputProducts.push_back(AZStd::move(interfaceProduct));
        response.m_resultCode = AssetBuilderSDK::ProcessJobResult_Success;
    }

    void MultiplayerScriptDefinedEditorSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        ReflectScriptDefinedAuthoring(context);
        MultiplayerScriptDefinedAsset::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<MultiplayerScriptDefinedEditorSystemComponent, AZ::Component>()->Version(1)->Attribute(
                AZ::Edit::Attributes::SystemComponentTags, AZStd::vector<AZ::Crc32>{ AZ_CRC_CE("AssetBuilder") });
        }
    }

    void MultiplayerScriptDefinedEditorSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AzFramework::s_GenericAssetRegistrar);
    }

    void MultiplayerScriptDefinedEditorSystemComponent::GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
        dependent.push_back(AZ_CRC_CE("AssetCatalogService"));
    }

    void MultiplayerScriptDefinedEditorSystemComponent::Activate()
    {
        m_handler = AZStd::make_unique<MultiplayerScriptDefinedAssetHandler>();
        m_handler->Register();

        AzToolsFramework::ToolsAssetSystemBus::Broadcast(
            &AzToolsFramework::ToolsAssetSystemRequests::RegisterSourceAssetType,
            azrtti_typeid<MultiplayerScriptDefinedAsset>(),
            ScriptDefinedSchemaExtension);

        AssetBuilderSDK::AssetBuilderDesc descriptor;
        descriptor.m_name = "Multiplayer Script Definition";
        descriptor.m_busId = azrtti_typeid<MultiplayerScriptDefinedBuilder>();
        descriptor.m_version = SchemaBuilderVersion;
        descriptor.m_patterns.emplace_back(
            AZStd::string::format("*%s", ScriptDefinedSchemaDotExtension), AssetBuilderSDK::AssetBuilderPattern::PatternType::Wildcard);
        descriptor.m_createJobFunction =
            [this](const AssetBuilderSDK::CreateJobsRequest& request, AssetBuilderSDK::CreateJobsResponse& response)
        {
            m_builder.CreateJobs(request, response);
        };
        descriptor.m_processJobFunction =
            [this](const AssetBuilderSDK::ProcessJobRequest& request, AssetBuilderSDK::ProcessJobResponse& response)
        {
            m_builder.ProcessJob(request, response);
        };

        m_builder.BusConnect(descriptor.m_busId);
        AssetBuilderSDK::AssetBuilderBus::Broadcast(&AssetBuilderSDK::AssetBuilderBus::Events::RegisterBuilderInformation, descriptor);
    }

    void MultiplayerScriptDefinedEditorSystemComponent::Deactivate()
    {
        m_builder.BusDisconnect();
        AzToolsFramework::ToolsAssetSystemBus::Broadcast(
            &AzToolsFramework::ToolsAssetSystemRequests::UnregisterSourceAssetType, azrtti_typeid<MultiplayerScriptDefinedAsset>());
        m_handler.reset();
    }
} // namespace ScriptCanvasMultiplayer
