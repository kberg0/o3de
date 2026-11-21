/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AssetBuilderSDK/AssetBuilderBusses.h>
#include <AssetBuilderSDK/AssetBuilderSDK.h>
#include <AzCore/std/parallel/atomic.h>
#include <AzFramework/Asset/GenericAssetHandler.h>
#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>
#include <Multiplayer/MultiplayerScriptDefinedDefinition.h>

namespace ScriptCanvasMultiplayer
{
    //! Loads and saves .mpschema sources for the Asset Editor.
    class MultiplayerScriptDefinedAssetHandler : public AzFramework::GenericAssetHandler<Multiplayer::MultiplayerScriptDefinedAsset>
    {
    public:
        MultiplayerScriptDefinedAssetHandler();

        //! Saves a valid schema and increases its revision, because Script Events uses the highest registered version.
        //! Refuses invalid schemas and schemas whose revision cannot increase.
        bool SaveAssetData(const AZ::Data::Asset<AZ::Data::AssetData>& asset, AZ::IO::GenericStream* stream) override;
    };

    //! A prefab-local value for one archetype field of the schema.
    struct ScriptDefinedArchetypeOverride
    {
        AZ_TYPE_INFO(ScriptDefinedArchetypeOverride, "{2ECBE0CD-1188-4E9D-B100-972468BC377F}");
        static void Reflect(AZ::ReflectContext* context);

        //! Label shown for this archetype in property editors.
        AZStd::string GetLabel() const
        {
            return m_name;
        }

        AZ::Uuid m_id;
        AZStd::string m_name;
        AZStd::string m_value;
        bool m_override = false;
    };

    //! Editor component that selects a schema asset and exports the runtime script-defined components.
    class EditorMultiplayerScriptDefinedComponent
        : public AzToolsFramework::Components::EditorComponentBase
        , private AZ::Data::AssetBus::Handler
    {
    public:
        AZ_EDITOR_COMPONENT(EditorMultiplayerScriptDefinedComponent, "{310C3322-A780-4747-9B30-02D82F634621}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        ~EditorMultiplayerScriptDefinedComponent() override;

        //! Selects the schema asset and refreshes the archetype list and validation status.
        void SetDefinitionAsset(AZ::Data::Asset<Multiplayer::MultiplayerScriptDefinedAsset> asset);

        //! Returns the schema's definition with the archetype overrides applied and the asset's source ID as its ID,
        //! or a description of why there is no valid definition.
        AZ::Outcome<Multiplayer::MultiplayerScriptDefinedDefinition, AZStd::string> ResolveDefinition() const;

        // AZ::Component overrides
        void Activate() override;
        void Deactivate() override;

        // EditorComponentBase overrides
        //! Adds the script-defined component, the input component when the schema declares inputs, and the bridge.
        void BuildGameEntity(AZ::Entity* gameEntity) override;

    private:
        AZ::u32 OnDefinitionChanged();
        void RefreshDefinition();

        // AZ::Data::AssetBus::Handler overrides
        void OnAssetReady(AZ::Data::Asset<AZ::Data::AssetData> asset) override;
        void OnAssetReloaded(AZ::Data::Asset<AZ::Data::AssetData> asset) override;

        AZ::Data::Asset<Multiplayer::MultiplayerScriptDefinedAsset> m_asset{ AZ::Data::AssetLoadBehavior::PreLoad };
        AZStd::vector<ScriptDefinedArchetypeOverride> m_archetypes;
        AZStd::string m_status = "Select a Multiplayer Script Definition asset";
    };

    //! Asset builder that compiles a .mpschema source into the schema product and its generated Script Events interface.
    class MultiplayerScriptDefinedBuilder : public AssetBuilderSDK::AssetBuilderCommandBus::Handler
    {
    public:
        AZ_TYPE_INFO(MultiplayerScriptDefinedBuilder, "{7378B478-6DAF-4EC3-ABF1-B661D826DB68}");

        void CreateJobs(const AssetBuilderSDK::CreateJobsRequest& request, AssetBuilderSDK::CreateJobsResponse& response) const;
        void ProcessJob(const AssetBuilderSDK::ProcessJobRequest& request, AssetBuilderSDK::ProcessJobResponse& response) const;

        // AssetBuilderSDK::AssetBuilderCommandBus::Handler overrides
        void ShutDown() override
        {
            m_shutdown = true;
        }

    private:
        AZStd::atomic_bool m_shutdown{ false };
    };

    //! Editor system component that registers the schema asset handler, source type and builder.
    class MultiplayerScriptDefinedEditorSystemComponent : public AZ::Component
    {
    public:
        AZ_COMPONENT(MultiplayerScriptDefinedEditorSystemComponent, "{5D5CA82A-739A-4ACF-85BE-9D26DC2F4D9C}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent);

        // AZ::Component overrides
        void Activate() override;
        void Deactivate() override;

    private:
        MultiplayerScriptDefinedBuilder m_builder;
        AZStd::unique_ptr<MultiplayerScriptDefinedAssetHandler> m_handler;
    };
} // namespace ScriptCanvasMultiplayer
