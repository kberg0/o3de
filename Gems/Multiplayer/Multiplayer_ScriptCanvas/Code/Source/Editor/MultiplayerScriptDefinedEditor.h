/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
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
    class MultiplayerScriptDefinedAssetHandler : public AzFramework::GenericAssetHandler<Multiplayer::MultiplayerScriptDefinedAsset>
    {
    public:
        MultiplayerScriptDefinedAssetHandler();
        bool SaveAssetData(const AZ::Data::Asset<AZ::Data::AssetData>& asset, AZ::IO::GenericStream* stream) override;
    };
    struct ScriptDefinedArchetypeOverride
    {
        AZ_TYPE_INFO(ScriptDefinedArchetypeOverride, "{2ECBE0CD-1188-4E9D-B100-972468BC377F}");
        AZ::Uuid m_id;
        AZStd::string m_name;
        AZStd::string m_value;
        bool m_override = false;
        AZStd::string GetLabel() const
        {
            return m_name;
        }
        static void Reflect(AZ::ReflectContext* context);
    };
    class EditorMultiplayerScriptDefinedComponent
        : public AzToolsFramework::Components::EditorComponentBase
        , private AZ::Data::AssetBus::Handler
    {
    public:
        AZ_EDITOR_COMPONENT(EditorMultiplayerScriptDefinedComponent, "{310C3322-A780-4747-9B30-02D82F634621}");
        ~EditorMultiplayerScriptDefinedComponent() override;
        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        void Activate() override;
        void Deactivate() override;
        void BuildGameEntity(AZ::Entity* entity) override;
        void SetDefinitionAsset(AZ::Data::Asset<Multiplayer::MultiplayerScriptDefinedAsset> asset);
        AZ::Outcome<Multiplayer::MultiplayerScriptDefinedDefinition, AZStd::string> ResolveDefinition() const;

    private:
        AZ::u32 OnDefinitionChanged();
        void RefreshDefinition();
        void OnAssetReady(AZ::Data::Asset<AZ::Data::AssetData> asset) override;
        void OnAssetReloaded(AZ::Data::Asset<AZ::Data::AssetData> asset) override;
        AZ::Data::Asset<Multiplayer::MultiplayerScriptDefinedAsset> m_asset{ AZ::Data::AssetLoadBehavior::PreLoad };
        AZStd::vector<ScriptDefinedArchetypeOverride> m_archetypes;
        AZStd::string m_status = "Select a Multiplayer Script Definition asset";
    };

    class MultiplayerScriptDefinedBuilder : public AssetBuilderSDK::AssetBuilderCommandBus::Handler
    {
    public:
        AZ_TYPE_INFO(MultiplayerScriptDefinedBuilder, "{7378B478-6DAF-4EC3-ABF1-B661D826DB68}");
        void ShutDown() override
        {
            m_shutdown = true;
        }
        void CreateJobs(const AssetBuilderSDK::CreateJobsRequest&, AssetBuilderSDK::CreateJobsResponse&) const;
        void ProcessJob(const AssetBuilderSDK::ProcessJobRequest&, AssetBuilderSDK::ProcessJobResponse&) const;

    private:
        AZStd::atomic_bool m_shutdown{ false };
    };

    class MultiplayerScriptDefinedEditorSystemComponent : public AZ::Component
    {
    public:
        AZ_COMPONENT(MultiplayerScriptDefinedEditorSystemComponent, "{5D5CA82A-739A-4ACF-85BE-9D26DC2F4D9C}");
        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        void Activate() override;
        void Deactivate() override;

    private:
        MultiplayerScriptDefinedBuilder m_builder;
        AZStd::unique_ptr<MultiplayerScriptDefinedAssetHandler> m_handler;
    };
} // namespace ScriptCanvasMultiplayer
