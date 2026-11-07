/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once

#include "MultiplayerScriptDefinedInterface.h"
#include <AzCore/Component/Component.h>
#include <ScriptEvents/ScriptEventRegistration.h>
#include <ScriptEvents/ScriptEventsAsset.h>

namespace ScriptCanvasMultiplayer
{
    class MultiplayerScriptDefinedBridge
        : public AZ::Component
        , private Multiplayer::ScriptDefinedNotificationBus::Handler
    {
    public:
        AZ_COMPONENT(MultiplayerScriptDefinedBridge, "{8E07F7E7-C3C2-4A6B-8BBF-5FDC6B4BE816}");
        static void Reflect(AZ::ReflectContext* context);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        void Configure(AZ::Data::Asset<ScriptEvents::ScriptEventsAsset> interfaceAsset);
        void Init() override;
        void Activate() override;
        void Deactivate() override;
        ~MultiplayerScriptDefinedBridge() override;

    private:
        bool Bind();
        void Unbind();
        static void Hook(void* instance, const char* name, int, AZ::BehaviorArgument* result, int count, AZ::BehaviorArgument* arguments);
        void Dispatch(const char* name, AZ::BehaviorArgument* result, int count, AZ::BehaviorArgument* arguments);
        void Notify(
            const AZStd::string& name,
            const AZStd::fixed_vector<Multiplayer::ScriptDefinedRpcValue, Multiplayer::ScriptDefinedArgumentCapacity>& values);
        void OnPropertyChanged(const AZ::Uuid& id, const Multiplayer::ScriptDefinedRpcValue& value) override;
        void OnRpcReceived(uint16_t id, const Multiplayer::ScriptDefinedRpcPayload& payload) override;
        void OnCreateInput(float deltaTime) override;
        void OnProcessInput(float deltaTime, bool reprocessing) override;
        const Multiplayer::MultiplayerScriptDefinedDefinition* m_definition = nullptr;
        AZ::Data::Asset<ScriptEvents::ScriptEventsAsset> m_interfaceAsset{ AZ::Data::AssetLoadBehavior::PreLoad };
        AZStd::intrusive_ptr<ScriptEvents::Internal::ScriptEventRegistration> m_registration;
        AZ::BehaviorEBus* m_bus = nullptr;
        AZ::BehaviorEBusHandler* m_handler = nullptr;
        AZStd::vector<ScriptDefinedMethod> m_methods;
    };
} // namespace ScriptCanvasMultiplayer
