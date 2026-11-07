/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once

#include <Multiplayer/MultiplayerScriptDefinedDefinition.h>
#include <Source/AutoGen/MultiplayerScriptDefinedComponent.AutoComponent.h>

namespace Multiplayer
{
    class MultiplayerScriptDefinedComponent
        : public MultiplayerScriptDefinedComponentBase
        , private ScriptDefinedRequestBus::Handler
    {
    public:
        AZ_MULTIPLAYER_COMPONENT(
            MultiplayerScriptDefinedComponent, s_multiplayerScriptDefinedComponentConcreteUuid, MultiplayerScriptDefinedComponentBase);
        static void Reflect(AZ::ReflectContext* context);
        void OnInit() override;
        void OnActivate(EntityIsMigrating) override;
        void OnDeactivate(EntityIsMigrating) override;
        using RpcReceivedEvent = AZ::Event<AzNetworking::IConnection*, const ScriptDefinedRpcPayload&>;
        bool ConfigureDefinition(const MultiplayerScriptDefinedDefinition& definition);
        bool ConfigureRpcSchema(AZ::u64 fingerprint, const AZStd::vector<ScriptDefinedRpcSignature>& signatures);
        bool SendRpc(const ScriptDefinedRpcPayload& payload);
        void AddRpcReceivedHandler(RpcReceivedEvent::Handler& handler);
        const MultiplayerScriptDefinedDefinition* GetDefinition() const override;
        bool ReadProperty(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const override;
        bool WriteProperty(const AZ::Uuid& id, const ScriptDefinedRpcValue& value) override;
        bool ReadArchetype(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const override;
        bool InvokeRpc(
            const AZ::Uuid& id, const AZStd::fixed_vector<ScriptDefinedRpcValue, ScriptDefinedArgumentCapacity>& arguments) override;
        bool SerializeStateDeltaMessage(ReplicationRecord& record, AzNetworking::ISerializer& serializer) override;
        void NotifyStateDeltaChanges(ReplicationRecord& record) override;
#if AZ_TRAIT_CLIENT
        void HandleDispatch(AzNetworking::IConnection*, const ScriptDefinedRpcPayload&) override;
        void HandleDispatchAuthorityToClientUnreliable(AzNetworking::IConnection*, const ScriptDefinedRpcPayload&) override;
#endif
        void ReceiveRpc(AzNetworking::IConnection*, const ScriptDefinedRpcPayload&, RpcDeliveryType, bool reliable);

    protected:
        void NetworkAttach(NetBindComponent*, ReplicationRecord&, ReplicationRecord&) override;

    private:
        void InitializeProperties(const MultiplayerScriptDefinedDefinition& definition);
        bool MatchesRpcSchema(const ScriptDefinedRpcPayload& payload) const;
        MultiplayerScriptDefinedDefinition m_definition;
        bool m_hasDefinition = false;
        AZ::u64 m_schemaFingerprint = 0;
        AZStd::vector<ScriptDefinedRpcSignature> m_rpcSignatures;
        bool m_configurationLocked = false;
        RpcReceivedEvent m_rpcReceivedEvent;
    };

    class MultiplayerScriptDefinedComponentController : public MultiplayerScriptDefinedComponentControllerBase
    {
    public:
        explicit MultiplayerScriptDefinedComponentController(MultiplayerScriptDefinedComponent& parent)
            : MultiplayerScriptDefinedComponentControllerBase(parent)
        {
        }
        void OnActivate(EntityIsMigrating) override
        {
        }
        void OnDeactivate(EntityIsMigrating) override
        {
        }
        bool IsInputEnabled() const override
        {
            return false;
        }
        bool TrySetValue(uint32_t index, float value);
#if AZ_TRAIT_SERVER
        void HandleDispatchAutonomousToAuthority(AzNetworking::IConnection*, const ScriptDefinedRpcPayload&) override;
        void HandleDispatchAutonomousToAuthorityUnreliable(AzNetworking::IConnection*, const ScriptDefinedRpcPayload&) override;
        void HandleDispatchServerToAuthority(AzNetworking::IConnection*, const ScriptDefinedRpcPayload&) override;
        void HandleDispatchServerToAuthorityUnreliable(AzNetworking::IConnection*, const ScriptDefinedRpcPayload&) override;
#endif
#if AZ_TRAIT_CLIENT
        void HandleDispatchAuthorityToAutonomous(AzNetworking::IConnection*, const ScriptDefinedRpcPayload&) override;
        void HandleDispatchAuthorityToAutonomousUnreliable(AzNetworking::IConnection*, const ScriptDefinedRpcPayload&) override;
#endif
    };
} // namespace Multiplayer
