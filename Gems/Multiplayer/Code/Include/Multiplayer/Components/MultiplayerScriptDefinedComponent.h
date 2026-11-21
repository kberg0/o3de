/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <Multiplayer/MultiplayerScriptDefinedDefinition.h>
#include <Source/AutoGen/MultiplayerScriptDefinedComponent.AutoComponent.h>

namespace Multiplayer
{
    //! Replicates the properties and carries the RPCs of a schema authored without code.
    //! Properties live in fixed-size banks, one per value type, so that one compiled component serves every schema.
    //! Properties marked rewindable share one more bank that keeps a rewind history, so that server-side lag compensation
    //! reads them as they were on the frame being rewound to; rewindable values are readable from network attachment on.
    //! Configure the component before it attaches to the network; its configuration is locked from then on.
    class MultiplayerScriptDefinedComponent
        : public MultiplayerScriptDefinedComponentBase
        , private ScriptDefinedRequestBus::Handler
    {
    public:
        AZ_MULTIPLAYER_COMPONENT(
            MultiplayerScriptDefinedComponent, s_multiplayerScriptDefinedComponentConcreteUuid, MultiplayerScriptDefinedComponentBase);

        //! Signaled with the sending connection when an RPC that matches the schema is received.
        using RpcReceivedEvent = AZ::Event<AzNetworking::IConnection*, const ScriptDefinedRpcPayload&>;

        static void Reflect(AZ::ReflectContext* context);

        //! Configures the component from a definition and seeds the property defaults.
        //! Returns false if the definition is invalid or the component is already attached to the network.
        bool ConfigureDefinition(const MultiplayerScriptDefinedDefinition& definition);

        //! Configures only the RPC schema, for native callers without a definition.
        //! Returns false if the schema is invalid, a definition is configured, or the configuration is locked.
        bool ConfigureRpcSchema(AZ::u64 fingerprint, const AZStd::vector<ScriptDefinedRpcSignature>& signatures);

        //! Sends an RPC on the route of its signature. Returns false if the payload does not match the schema
        //! or this peer's role cannot send it.
        bool SendRpc(const ScriptDefinedRpcPayload& payload);

        //! Connects a handler to RPCs received by this component.
        void AddRpcReceivedHandler(RpcReceivedEvent::Handler& handler);

        //! Dispatches a received RPC if it matches the schema and arrived on the route and reliability of its signature.
        void ReceiveRpc(
            AzNetworking::IConnection* connection, const ScriptDefinedRpcPayload& payload, RpcDeliveryType route, bool reliable);

        // MultiplayerComponent overrides
        void OnInit() override;
        void OnActivate(EntityIsMigrating entityIsMigrating) override;
        void OnDeactivate(EntityIsMigrating entityIsMigrating) override;
        bool SerializeStateDeltaMessage(ReplicationRecord& replicationRecord, AzNetworking::ISerializer& serializer) override;
        void NotifyStateDeltaChanges(ReplicationRecord& replicationRecord) override;

        // ScriptDefinedRequestBus::Handler overrides
        const MultiplayerScriptDefinedDefinition* GetDefinition() const override;
        bool ReadProperty(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const override;
        bool WriteProperty(const AZ::Uuid& id, const ScriptDefinedRpcValue& value) override;
        bool ReadArchetype(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const override;
        bool InvokeRpc(
            const AZ::Uuid& id, const AZStd::fixed_vector<ScriptDefinedRpcValue, ScriptDefinedArgumentCapacity>& arguments) override;

#if AZ_TRAIT_CLIENT
        // MultiplayerScriptDefinedComponentBase RPC handlers
        void HandleDispatch(AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload) override;
        void HandleDispatchAuthorityToClientUnreliable(
            AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload) override;
#endif

    protected:
        // MultiplayerComponent overrides
        void NetworkAttach(
            NetBindComponent* netBindComponent,
            ReplicationRecord& currentEntityRecord,
            ReplicationRecord& predictableEntityRecord) override;

    private:
        //! Seeds the typed banks with the definition's defaults.
        void InitializeProperties(const MultiplayerScriptDefinedDefinition& definition);

        //! Seeds the rewindable bank with the definition's defaults. Needs network time, so it runs at network attachment.
        void InitializeRewindableProperties(const MultiplayerScriptDefinedDefinition& definition);

        //! Returns true while time is rewound for a connection other than this entity's owner, when rewindable
        //! properties read their history and cannot be written.
        bool IsRewoundForAnotherConnection() const;

        bool MatchesRpcSchema(const ScriptDefinedRpcPayload& payload) const;

        MultiplayerScriptDefinedDefinition m_definition;
        bool m_hasDefinition = false;
        AZ::u64 m_schemaFingerprint = 0;
        AZStd::vector<ScriptDefinedRpcSignature> m_rpcSignatures;
        bool m_configurationLocked = false;
        RpcReceivedEvent m_rpcReceivedEvent;
    };

    //! Controller of MultiplayerScriptDefinedComponent. It owns property writes and handles the RPCs sent to the authority
    //! and the autonomous peer. It processes no input itself; MultiplayerScriptDefinedInputComponent does.
    class MultiplayerScriptDefinedComponentController : public MultiplayerScriptDefinedComponentControllerBase
    {
    public:
        explicit MultiplayerScriptDefinedComponentController(MultiplayerScriptDefinedComponent& parent)
            : MultiplayerScriptDefinedComponentControllerBase(parent)
        {
        }

        //! Writes a slot of the Float bank on the authority. Returns false on other peers, an out-of-range slot or a non-finite value.
        bool TrySetValue(uint32_t index, float value);

        // MultiplayerController overrides
        void OnActivate([[maybe_unused]] EntityIsMigrating entityIsMigrating) override
        {
        }

        void OnDeactivate([[maybe_unused]] EntityIsMigrating entityIsMigrating) override
        {
        }

        bool IsInputEnabled() const override
        {
            return false;
        }

#if AZ_TRAIT_SERVER
        // MultiplayerScriptDefinedComponentControllerBase RPC handlers
        void HandleDispatchAutonomousToAuthority(
            AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload) override;
        void HandleDispatchAutonomousToAuthorityUnreliable(
            AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload) override;
        void HandleDispatchServerToAuthority(
            AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload) override;
        void HandleDispatchServerToAuthorityUnreliable(
            AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload) override;
#endif

#if AZ_TRAIT_CLIENT
        // MultiplayerScriptDefinedComponentControllerBase RPC handlers
        void HandleDispatchAuthorityToAutonomous(
            AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload) override;
        void HandleDispatchAuthorityToAutonomousUnreliable(
            AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload) override;
#endif
    };
} // namespace Multiplayer
