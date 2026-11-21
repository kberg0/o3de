/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/containers/array.h>
#include <AzCore/std/typetraits/remove_cvref.h>
#include <AzCore/std/utility/declval.h>
#include <Multiplayer/Components/MultiplayerScriptDefinedComponent.h>
#include <Multiplayer/Components/NetBindComponent.h>
#include <Multiplayer/NetworkEntity/EntityReplication/ReplicationRecord.h>
#include <Multiplayer/NetworkTime/INetworkTime.h>
#include <cmath>

namespace Multiplayer
{
    namespace
    {
        using ComponentBase = MultiplayerScriptDefinedComponentBase;

        // The banks in MultiplayerScriptDefinedComponent.AutoComponent.xml must match MULTIPLAYER_SCRIPT_DEFINED_TYPES:
        // one {EnumName}Values bank of ScriptDefinedPropertyCapacity elements per type, plus the rewindable bank.
#define MULTIPLAYER_SCRIPT_DEFINED_CHECK_BANK(EnumName, CppType)                                                                           \
    static_assert(                                                                                                                         \
        AZStd::is_same_v<                                                                                                                  \
            AZStd::remove_cvref_t<decltype(AZStd::declval<const ComponentBase&>().Get##EnumName##ValuesVector())>,                         \
            AZStd::fixed_vector<CppType, ScriptDefinedPropertyCapacity>>,                                                                  \
        "The " #EnumName "Values bank must hold ScriptDefinedPropertyCapacity values of type " #CppType);
        MULTIPLAYER_SCRIPT_DEFINED_TYPES(MULTIPLAYER_SCRIPT_DEFINED_CHECK_BANK)
#undef MULTIPLAYER_SCRIPT_DEFINED_CHECK_BANK

        static_assert(
            AZStd::is_same_v<
                AZStd::remove_cvref_t<decltype(AZStd::declval<const ComponentBase&>().GetRewindableValuesArray())>,
                RewindableArray<ScriptDefinedRpcValue, ScriptDefinedRewindableCapacity>>,
            "The RewindableValues bank must hold ScriptDefinedRewindableCapacity values");

        static_assert(
            ComponentBase::AuthorityToClientRecordBitCount == ScriptDefinedPropertyRecordBits,
            "The component must replicate only the property banks");

        //! First authority-to-client record bit of each typed bank, indexed by ScriptDefinedRpcType.
        constexpr AZStd::array<uint32_t, ScriptDefinedTypeCount> TypedBankFirstBits = {
#define MULTIPLAYER_SCRIPT_DEFINED_FIRST_BIT(EnumName, CppType) ComponentBase::EnumName##ValuesFirstRecordBit,
            MULTIPLAYER_SCRIPT_DEFINED_TYPES(MULTIPLAYER_SCRIPT_DEFINED_FIRST_BIT)
#undef MULTIPLAYER_SCRIPT_DEFINED_FIRST_BIT
        };

        //! Returns the record bit of a property's element, relative to the component's first authority-to-client bit.
        uint32_t PropertyElementBit(const ScriptDefinedField& field, const MultiplayerScriptDefinedDefinition& definition)
        {
            const uint32_t slot = definition.GetPropertySlot(field.m_id);
            if (field.m_rewindable)
            {
                return ComponentBase::RewindableValuesFirstRecordBit + slot;
            }
            return TypedBankFirstBits[static_cast<size_t>(field.m_type)] + slot;
        }

        //! Returns the record bit of the size of a typed bank. It follows the bank's element bits.
        uint32_t TypedBankSizeBit(ScriptDefinedRpcType type)
        {
            return TypedBankFirstBits[static_cast<size_t>(type)] + ScriptDefinedPropertyCapacity;
        }

        //! Sets the record bits of a property: its element and, for a typed bank, the bank's size.
        void SetPropertyBits(
            AzNetworking::IBitset& bits,
            uint32_t firstComponentBit,
            const ScriptDefinedField& field,
            const MultiplayerScriptDefinedDefinition& definition)
        {
            bits.SetBit(firstComponentBit + PropertyElementBit(field, definition), true);

            // The rewindable bank is a fixed-size array without a size bit.
            if (!field.m_rewindable)
            {
                bits.SetBit(firstComponentBit + TypedBankSizeBit(field.m_type), true);
            }
        }

        bool IsValidRpcSchema(AZ::u64 fingerprint, const AZStd::vector<ScriptDefinedRpcSignature>& signatures)
        {
            // A schema without RPCs needs no fingerprint; a schema with RPCs does.
            if ((fingerprint == 0 && !signatures.empty()) || signatures.size() > ScriptDefinedRpcCapacity)
            {
                return false;
            }

            for (size_t index = 0; index < signatures.size(); ++index)
            {
                const ScriptDefinedRpcSignature& signature = signatures[index];
                if (signature.m_rpcId == 0)
                {
                    return false;
                }

                switch (signature.m_delivery)
                {
                case RpcDeliveryType::AuthorityToClient:
                case RpcDeliveryType::AuthorityToAutonomous:
                case RpcDeliveryType::AutonomousToAuthority:
                case RpcDeliveryType::ServerToAuthority:
                    break;
                default:
                    return false;
                }

                for (ScriptDefinedRpcType type : signature.m_parameterTypes)
                {
                    if (static_cast<uint32_t>(type) >= ScriptDefinedTypeCount)
                    {
                        return false;
                    }
                }

                // RPC IDs must be unique.
                for (size_t previous = 0; previous < index; ++previous)
                {
                    if (signatures[previous].m_rpcId == signature.m_rpcId)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        //! Builds the initial contents of one typed bank: the parsed default of each non-rewindable property of the type, in slot
        //! order.
        template<ScriptDefinedRpcType Type>
        auto MakePropertyBank(const MultiplayerScriptDefinedDefinition& definition)
        {
            using ValueType = ScriptDefinedValueType<Type>;

            size_t propertyCount = 0;
            for (const auto& field : definition.m_properties)
            {
                if (field.m_type == Type && !field.m_rewindable)
                {
                    ++propertyCount;
                }
            }

            AZStd::fixed_vector<ValueType, ScriptDefinedPropertyCapacity> values;
            values.resize(propertyCount, AZStd::get<ValueType>(DefaultScriptDefinedValue(Type)));
            for (const auto& field : definition.m_properties)
            {
                if (field.m_type == Type && !field.m_rewindable)
                {
                    const ScriptDefinedRpcValue defaultValue = ParseScriptDefinedValue(Type, field.m_defaultValue).GetValue();
                    values[definition.GetPropertySlot(field.m_id)] = AZStd::get<ValueType>(defaultValue);
                }
            }
            return values;
        }

        //! Builds the initial contents of the rewindable bank: the parsed default of each rewindable property, in slot order.
        AZStd::array<ScriptDefinedRpcValue, ScriptDefinedRewindableCapacity> MakeRewindableBank(
            const MultiplayerScriptDefinedDefinition& definition)
        {
            AZStd::array<ScriptDefinedRpcValue, ScriptDefinedRewindableCapacity> values{};
            for (const auto& field : definition.m_properties)
            {
                if (field.m_rewindable)
                {
                    values[definition.GetPropertySlot(field.m_id)] = ParseScriptDefinedValue(field.m_type, field.m_defaultValue).GetValue();
                }
            }
            return values;
        }
    } // namespace

    void MultiplayerScriptDefinedComponent::Reflect(AZ::ReflectContext* context)
    {
        MultiplayerScriptDefinedDefinition::Reflect(context);
        ScriptDefinedRpcSignature::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<MultiplayerScriptDefinedComponent, MultiplayerScriptDefinedComponentBase>()
                ->Version(2)
                ->Field("Definition", &MultiplayerScriptDefinedComponent::m_definition)
                ->Field("HasDefinition", &MultiplayerScriptDefinedComponent::m_hasDefinition)
                ->Field("SchemaFingerprint", &MultiplayerScriptDefinedComponent::m_schemaFingerprint)
                ->Field("RpcSignatures", &MultiplayerScriptDefinedComponent::m_rpcSignatures);
        }

        // The generated base reflects the concrete class into EditContext.
        MultiplayerScriptDefinedComponentBase::Reflect(context);
    }

    void MultiplayerScriptDefinedComponent::OnInit()
    {
        ScriptDefinedRequestBus::Handler::BusConnect(GetEntityId());
    }

    void MultiplayerScriptDefinedComponent::OnActivate([[maybe_unused]] EntityIsMigrating entityIsMigrating)
    {
        if (!ScriptDefinedRequestBus::Handler::BusIsConnected())
        {
            ScriptDefinedRequestBus::Handler::BusConnect(GetEntityId());
        }
    }

    void MultiplayerScriptDefinedComponent::OnDeactivate([[maybe_unused]] EntityIsMigrating entityIsMigrating)
    {
        ScriptDefinedRequestBus::Handler::BusDisconnect();
    }

    bool MultiplayerScriptDefinedComponent::ConfigureDefinition(const MultiplayerScriptDefinedDefinition& definition)
    {
        if (m_configurationLocked || GetNetBindComponent() || !definition.Validate())
        {
            return false;
        }

        m_definition = definition;
        m_hasDefinition = true;
        m_schemaFingerprint = definition.GetFingerprint();
        m_rpcSignatures = definition.GetRpcSignatures();
        InitializeProperties(definition);
        return true;
    }

    void MultiplayerScriptDefinedComponent::InitializeProperties(const MultiplayerScriptDefinedDefinition& definition)
    {
#define MULTIPLAYER_SCRIPT_DEFINED_INITIALIZE(EnumName, CppType)                                                                           \
    Initialize##EnumName##Values(MakePropertyBank<ScriptDefinedRpcType::EnumName>(definition));
        MULTIPLAYER_SCRIPT_DEFINED_TYPES(MULTIPLAYER_SCRIPT_DEFINED_INITIALIZE)
#undef MULTIPLAYER_SCRIPT_DEFINED_INITIALIZE
    }

    void MultiplayerScriptDefinedComponent::InitializeRewindableProperties(const MultiplayerScriptDefinedDefinition& definition)
    {
        InitializeRewindableValues(MakeRewindableBank(definition));
    }

    bool MultiplayerScriptDefinedComponent::IsRewoundForAnotherConnection() const
    {
        // Mirrors RewindableObject: while time is rewound, only the rewinding connection's own entities stay in the present.
        const INetworkTime* networkTime = GetNetworkTime();
        const NetBindComponent* netBindComponent = GetNetBindComponent();
        return networkTime && netBindComponent && networkTime->IsTimeRewound() &&
            networkTime->GetRewindingConnectionId() != netBindComponent->GetOwningConnectionId();
    }

    bool MultiplayerScriptDefinedComponent::ConfigureRpcSchema(
        AZ::u64 fingerprint, const AZStd::vector<ScriptDefinedRpcSignature>& signatures)
    {
        if (m_configurationLocked || m_hasDefinition || !IsValidRpcSchema(fingerprint, signatures))
        {
            return false;
        }

        m_schemaFingerprint = fingerprint;
        m_rpcSignatures = signatures;
        return true;
    }

    const MultiplayerScriptDefinedDefinition* MultiplayerScriptDefinedComponent::GetDefinition() const
    {
        return m_hasDefinition ? &m_definition : nullptr;
    }

    bool MultiplayerScriptDefinedComponent::ReadProperty(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const
    {
        const ScriptDefinedField* field = m_definition.FindProperty(id);
        if (!m_hasDefinition || !field)
        {
            return false;
        }

        const uint32_t slot = m_definition.GetPropertySlot(id);
        if (field->m_rewindable)
        {
            // Rewindable values exist from network attachment on, when network time can record them.
            if (!m_configurationLocked || slot >= ScriptDefinedRewindableCapacity)
            {
                return false;
            }
            // The element replicates with its own type tag, so check it against the schema before handing it out.
            const ScriptDefinedRpcValue& stored = GetRewindableValues(slot);
            if (stored.index() != static_cast<size_t>(field->m_type))
            {
                return false;
            }
            value = stored;
            return true;
        }

        switch (field->m_type)
        {
#define MULTIPLAYER_SCRIPT_DEFINED_READ(EnumName, CppType)                                                                                 \
    case ScriptDefinedRpcType::EnumName:                                                                                                   \
        if (slot >= EnumName##ValuesGetSize())                                                                                             \
        {                                                                                                                                  \
            return false;                                                                                                                  \
        }                                                                                                                                  \
        value.emplace<CppType>(Get##EnumName##Values(slot));                                                                               \
        return true;
            MULTIPLAYER_SCRIPT_DEFINED_TYPES(MULTIPLAYER_SCRIPT_DEFINED_READ)
#undef MULTIPLAYER_SCRIPT_DEFINED_READ
        default:
            return false;
        }
    }

    bool MultiplayerScriptDefinedComponent::WriteProperty(const AZ::Uuid& id, const ScriptDefinedRpcValue& value)
    {
        const ScriptDefinedField* field = m_definition.FindProperty(id);
        if (!m_hasDefinition || !field || value.index() != static_cast<size_t>(field->m_type) || !GetController())
        {
            return false;
        }

        // The authority writes any property; the autonomous peer only writes predictable ones.
        const bool canWrite = IsNetEntityRoleAuthority() || (field->m_predictable && IsNetEntityRoleAutonomous());
        if (!canWrite || !IsFiniteScriptDefinedValue(value))
        {
            return false;
        }

        // A rewindable property cannot be written into its history; refuse while another connection has rewound time.
        if (field->m_rewindable && IsRewoundForAnotherConnection())
        {
            return false;
        }

        ScriptDefinedRpcValue previous;
        if (!ReadProperty(id, previous))
        {
            return false;
        }

        const uint32_t slot = m_definition.GetPropertySlot(id);
        auto* controller = static_cast<MultiplayerScriptDefinedComponentController*>(GetController());
        if (field->m_rewindable)
        {
            controller->SetRewindableValues(slot, value);
        }
        else
        {
            switch (field->m_type)
            {
#define MULTIPLAYER_SCRIPT_DEFINED_WRITE(EnumName, CppType)                                                                                \
    case ScriptDefinedRpcType::EnumName:                                                                                                   \
        controller->Set##EnumName##Values(slot, AZStd::get<CppType>(value));                                                               \
        break;
                MULTIPLAYER_SCRIPT_DEFINED_TYPES(MULTIPLAYER_SCRIPT_DEFINED_WRITE)
#undef MULTIPLAYER_SCRIPT_DEFINED_WRITE
            default:
                return false;
            }
        }

        if (previous != value)
        {
            ScriptDefinedNotificationBus::Event(GetEntityId(), &ScriptDefinedNotifications::OnPropertyChanged, id, value);
        }
        return true;
    }

    bool MultiplayerScriptDefinedComponent::ReadArchetype(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const
    {
        const ScriptDefinedField* field = m_definition.FindArchetype(id);
        if (!m_hasDefinition || !field)
        {
            return false;
        }

        auto parsed = ParseScriptDefinedValue(field->m_type, field->m_defaultValue);
        if (!parsed)
        {
            return false;
        }

        value = parsed.TakeValue();
        return true;
    }

    bool MultiplayerScriptDefinedComponent::InvokeRpc(
        const AZ::Uuid& id, const AZStd::fixed_vector<ScriptDefinedRpcValue, ScriptDefinedArgumentCapacity>& arguments)
    {
        if (!m_hasDefinition)
        {
            return false;
        }

        ScriptDefinedRpcPayload payload;
        payload.m_schemaFingerprint = m_schemaFingerprint;
        payload.m_rpcId = m_definition.GetRpcId(id);
        payload.m_arguments = arguments;
        return SendRpc(payload);
    }

    bool MultiplayerScriptDefinedComponent::MatchesRpcSchema(const ScriptDefinedRpcPayload& payload) const
    {
        if (m_schemaFingerprint == 0 || payload.m_schemaFingerprint != m_schemaFingerprint || !payload.IsValid())
        {
            return false;
        }

        for (const auto& signature : m_rpcSignatures)
        {
            if (payload.MatchesSignature(signature))
            {
                return true;
            }
        }
        return false;
    }

    bool MultiplayerScriptDefinedComponent::SendRpc(const ScriptDefinedRpcPayload& payload)
    {
        if (!MatchesRpcSchema(payload))
        {
            return false;
        }

        [[maybe_unused]] auto* controller = static_cast<MultiplayerScriptDefinedComponentController*>(GetController());
        for (const auto& signature : m_rpcSignatures)
        {
            if (!payload.MatchesSignature(signature))
            {
                continue;
            }

            // Send only on the signature's route, and only from the role that route starts at.
            switch (signature.m_delivery)
            {
#if AZ_TRAIT_SERVER
            case RpcDeliveryType::AuthorityToClient:
                if (IsNetEntityRoleAuthority() && controller)
                {
                    if (signature.m_isReliable)
                    {
                        controller->Dispatch(payload);
                    }
                    else
                    {
                        controller->DispatchAuthorityToClientUnreliable(payload);
                    }
                    return true;
                }
                break;
            case RpcDeliveryType::AuthorityToAutonomous:
                if (IsNetEntityRoleAuthority() && controller)
                {
                    if (signature.m_isReliable)
                    {
                        controller->DispatchAuthorityToAutonomous(payload);
                    }
                    else
                    {
                        controller->DispatchAuthorityToAutonomousUnreliable(payload);
                    }
                    return true;
                }
                break;
            case RpcDeliveryType::ServerToAuthority:
                if (IsNetEntityRoleServer() || IsNetEntityRoleAuthority())
                {
                    if (signature.m_isReliable)
                    {
                        DispatchServerToAuthority(payload);
                    }
                    else
                    {
                        DispatchServerToAuthorityUnreliable(payload);
                    }
                    return true;
                }
                break;
#endif
#if AZ_TRAIT_CLIENT
            case RpcDeliveryType::AutonomousToAuthority:
                if (IsNetEntityRoleAutonomous() && controller)
                {
                    if (signature.m_isReliable)
                    {
                        controller->DispatchAutonomousToAuthority(payload);
                    }
                    else
                    {
                        controller->DispatchAutonomousToAuthorityUnreliable(payload);
                    }
                    return true;
                }
                break;
#endif
            default:
                break;
            }
        }
        return false;
    }

    void MultiplayerScriptDefinedComponent::ReceiveRpc(
        AzNetworking::IConnection* connection, const ScriptDefinedRpcPayload& payload, RpcDeliveryType route, bool reliable)
    {
        if (!MatchesRpcSchema(payload))
        {
            return;
        }

        for (const auto& signature : m_rpcSignatures)
        {
            if (payload.MatchesSignature(signature) && route == signature.m_delivery && reliable == signature.m_isReliable)
            {
                m_rpcReceivedEvent.Signal(connection, payload);
                ScriptDefinedNotificationBus::Event(GetEntityId(), &ScriptDefinedNotifications::OnRpcReceived, payload.m_rpcId, payload);
                return;
            }
        }
    }

#if AZ_TRAIT_CLIENT
    void MultiplayerScriptDefinedComponent::HandleDispatch(
        AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload)
    {
        ReceiveRpc(invokingConnection, payload, RpcDeliveryType::AuthorityToClient, true);
    }

    void MultiplayerScriptDefinedComponent::HandleDispatchAuthorityToClientUnreliable(
        AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload)
    {
        ReceiveRpc(invokingConnection, payload, RpcDeliveryType::AuthorityToClient, false);
    }

    void MultiplayerScriptDefinedComponentController::HandleDispatchAuthorityToAutonomous(
        AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload)
    {
        GetParent().ReceiveRpc(invokingConnection, payload, RpcDeliveryType::AuthorityToAutonomous, true);
    }

    void MultiplayerScriptDefinedComponentController::HandleDispatchAuthorityToAutonomousUnreliable(
        AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload)
    {
        GetParent().ReceiveRpc(invokingConnection, payload, RpcDeliveryType::AuthorityToAutonomous, false);
    }
#endif

#if AZ_TRAIT_SERVER
    void MultiplayerScriptDefinedComponentController::HandleDispatchAutonomousToAuthority(
        AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload)
    {
        GetParent().ReceiveRpc(invokingConnection, payload, RpcDeliveryType::AutonomousToAuthority, true);
    }

    void MultiplayerScriptDefinedComponentController::HandleDispatchAutonomousToAuthorityUnreliable(
        AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload)
    {
        GetParent().ReceiveRpc(invokingConnection, payload, RpcDeliveryType::AutonomousToAuthority, false);
    }

    void MultiplayerScriptDefinedComponentController::HandleDispatchServerToAuthority(
        AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload)
    {
        GetParent().ReceiveRpc(invokingConnection, payload, RpcDeliveryType::ServerToAuthority, true);
    }

    void MultiplayerScriptDefinedComponentController::HandleDispatchServerToAuthorityUnreliable(
        AzNetworking::IConnection* invokingConnection, const ScriptDefinedRpcPayload& payload)
    {
        GetParent().ReceiveRpc(invokingConnection, payload, RpcDeliveryType::ServerToAuthority, false);
    }
#endif

    void MultiplayerScriptDefinedComponent::AddRpcReceivedHandler(RpcReceivedEvent::Handler& handler)
    {
        handler.Connect(m_rpcReceivedEvent);
    }

    void MultiplayerScriptDefinedComponent::NetworkAttach(
        NetBindComponent* netBindComponent, ReplicationRecord& currentEntityRecord, ReplicationRecord& predictableEntityRecord)
    {
        // Drop a persisted configuration that is no longer consistent rather than replicate with it.
        const bool schemaValid = IsValidRpcSchema(m_schemaFingerprint, m_rpcSignatures);
        const bool definitionValid = !m_hasDefinition || (m_definition.Validate() && m_definition.GetFingerprint() == m_schemaFingerprint);
        if (!schemaValid || !definitionValid)
        {
            m_hasDefinition = false;
            m_schemaFingerprint = 0;
            m_rpcSignatures.clear();
        }

        // Hidden network banks are not serialized by the generated base. Rebuild their sizes and defaults from the
        // persisted definition before attachment registers dirty bits or a first snapshot applies replicated values.
        if (m_hasDefinition && !m_configurationLocked)
        {
            InitializeProperties(m_definition);
            InitializeRewindableProperties(m_definition);
        }
        m_configurationLocked = true;

        const uint32_t firstComponentBit = currentEntityRecord.m_authorityToClient.GetValidBitCount();
        MultiplayerScriptDefinedComponentBase::NetworkAttach(netBindComponent, currentEntityRecord, predictableEntityRecord);

        if (m_hasDefinition)
        {
            // The compiled banks support prediction, but only authored fields marked predictable belong in input
            // hashes and corrections. Keep the size bit when a bank contains at least one predicted element.
            for (uint32_t bit = 0; bit < ScriptDefinedPropertyRecordBits; ++bit)
            {
                predictableEntityRecord.m_authorityToClient.SetBit(firstComponentBit + bit, false);
            }

            for (const auto& field : m_definition.m_properties)
            {
                if (field.m_predictable)
                {
                    SetPropertyBits(predictableEntityRecord.m_authorityToClient, firstComponentBit, field, m_definition);
                }
            }
        }

#if AZ_TRAIT_SERVER
        if (m_hasDefinition && IsNetEntityRoleAuthority())
        {
            // Attachment precedes controller construction. Mark only populated slots and their size fields.
            for (const auto& field : m_definition.m_properties)
            {
                SetPropertyBits(currentEntityRecord.m_authorityToClient, firstComponentBit, field, m_definition);
            }
        }
#endif
    }

    bool MultiplayerScriptDefinedComponent::SerializeStateDeltaMessage(
        ReplicationRecord& replicationRecord, AzNetworking::ISerializer& serializer)
    {
        bool hasPropertyChanges = false;
        if (replicationRecord.ContainsAuthorityToClientBits())
        {
            if (replicationRecord.GetRemainingAuthorityToClientBits() < ScriptDefinedPropertyRecordBits)
            {
                serializer.Invalidate();
                return false;
            }

            for (uint32_t bit = 0; bit < ScriptDefinedPropertyRecordBits; ++bit)
            {
                hasPropertyChanges |= replicationRecord.m_authorityToClient.GetBit(replicationRecord.m_authorityToClientConsumedBits + bit);
            }
        }

        // Property changes carry the schema fingerprint, so a peer with another schema rejects them.
        if (hasPropertyChanges)
        {
            AZ::u64 fingerprint = m_schemaFingerprint;
            if (!serializer.Serialize(fingerprint, "ScriptSchema") || fingerprint != m_schemaFingerprint)
            {
                serializer.Invalidate();
                return false;
            }
        }

        return MultiplayerScriptDefinedComponentBase::SerializeStateDeltaMessage(replicationRecord, serializer);
    }

    void MultiplayerScriptDefinedComponent::NotifyStateDeltaChanges(ReplicationRecord& replicationRecord)
    {
        const uint32_t firstComponentBit = replicationRecord.m_authorityToClientConsumedBits;
        const bool hasBits = replicationRecord.ContainsAuthorityToClientBits();
        MultiplayerScriptDefinedComponentBase::NotifyStateDeltaChanges(replicationRecord);

        if (!m_hasDefinition || !hasBits)
        {
            return;
        }

        for (const auto& field : m_definition.m_properties)
        {
            if (!replicationRecord.m_authorityToClient.GetBit(firstComponentBit + PropertyElementBit(field, m_definition)))
            {
                continue;
            }

            ScriptDefinedRpcValue value;
            if (ReadProperty(field.m_id, value))
            {
                ScriptDefinedNotificationBus::Event(GetEntityId(), &ScriptDefinedNotifications::OnPropertyChanged, field.m_id, value);
            }
        }
    }

    bool MultiplayerScriptDefinedComponentController::TrySetValue(uint32_t index, float value)
    {
        if (IsNetEntityRoleAuthority() && index < FloatValuesGetSize() && std::isfinite(value))
        {
            SetFloatValues(index, value);
            return true;
        }
        return false;
    }
} // namespace Multiplayer
