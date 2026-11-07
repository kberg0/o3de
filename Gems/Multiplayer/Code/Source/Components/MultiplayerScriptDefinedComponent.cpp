/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/Serialization/SerializeContext.h>
#include <Multiplayer/Components/MultiplayerScriptDefinedComponent.h>
#include <Multiplayer/Components/NetBindComponent.h>
#include <Multiplayer/NetworkEntity/EntityReplication/ReplicationRecord.h>
#include <cmath>

namespace Multiplayer
{
    namespace
    {
        bool IsValidRpcSchema(AZ::u64 fingerprint, const AZStd::vector<ScriptDefinedRpcSignature>& signatures)
        {
            if ((fingerprint == 0 && !signatures.empty()) || signatures.size() > ScriptDefinedRpcCapacity)
            {
                return false;
            }
            for (size_t i = 0; i < signatures.size(); ++i)
            {
                const auto& signature = signatures[i];
                if (!signature.m_rpcId)
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
                for (auto type : signature.m_parameterTypes)
                {
                    if (type > ScriptDefinedRpcType::Entity)
                    {
                        return false;
                    }
                }
                for (size_t j = 0; j < i; ++j)
                {
                    if (signatures[j].m_rpcId == signature.m_rpcId)
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        template<ScriptDefinedRpcType Type>
        auto MakePropertyVector(const MultiplayerScriptDefinedDefinition& definition)
        {
            using T = AZStd::variant_alternative_t<static_cast<size_t>(Type), ScriptDefinedRpcValue>;
            AZStd::fixed_vector<T, ScriptDefinedPropertyCapacity> values;
            size_t count = 0;
            for (const auto& field : definition.m_properties)
            {
                count += field.m_type == Type ? 1 : 0;
            }
            values.resize(count, AZStd::get<T>(DefaultScriptDefinedValue(Type)));
            for (const auto& field : definition.m_properties)
            {
                if (field.m_type == Type)
                {
                    values[definition.GetPropertySlot(field.m_id)] =
                        AZStd::get<T>(ParseScriptDefinedValue(Type, field.m_defaultValue).GetValue());
                }
            }
            return values;
        }

        uint32_t PropertyBit(const ScriptDefinedField& field, const MultiplayerScriptDefinedDefinition& definition)
        {
            constexpr uint32_t banks[] = { 1, 6, 0, 10, 2, 3, 4, 5, 7, 8, 9, 11, 12, 13, 14, 15 };
            return banks[static_cast<unsigned>(field.m_type)] * (ScriptDefinedPropertyCapacity + 1) +
                definition.GetPropertySlot(field.m_id);
        }
    } // namespace
    void MultiplayerScriptDefinedComponent::Reflect(AZ::ReflectContext* context)
    {
        MultiplayerScriptDefinedDefinition::Reflect(context);
        ScriptDefinedRpcSignature::Reflect(context);
        if (auto* sc = azrtti_cast<AZ::SerializeContext*>(context))
        {
            sc->Class<MultiplayerScriptDefinedComponent, MultiplayerScriptDefinedComponentBase>()
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
    void MultiplayerScriptDefinedComponent::OnActivate(EntityIsMigrating)
    {
        if (!ScriptDefinedRequestBus::Handler::BusIsConnected())
        {
            ScriptDefinedRequestBus::Handler::BusConnect(GetEntityId());
        }
    }
    void MultiplayerScriptDefinedComponent::OnDeactivate(EntityIsMigrating)
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
        InitializeBoolValues(MakePropertyVector<ScriptDefinedRpcType::Boolean>(definition));
        InitializeInt32Values(MakePropertyVector<ScriptDefinedRpcType::Int32>(definition));
        InitializeValues(MakePropertyVector<ScriptDefinedRpcType::Float>(definition));
        InitializeVector3Values(MakePropertyVector<ScriptDefinedRpcType::Vector3>(definition));
        InitializeInt8Values(MakePropertyVector<ScriptDefinedRpcType::Int8>(definition));
        InitializeUInt8Values(MakePropertyVector<ScriptDefinedRpcType::UInt8>(definition));
        InitializeInt16Values(MakePropertyVector<ScriptDefinedRpcType::Int16>(definition));
        InitializeUInt16Values(MakePropertyVector<ScriptDefinedRpcType::UInt16>(definition));
        InitializeUInt32Values(MakePropertyVector<ScriptDefinedRpcType::UInt32>(definition));
        InitializeInt64Values(MakePropertyVector<ScriptDefinedRpcType::Int64>(definition));
        InitializeUInt64Values(MakePropertyVector<ScriptDefinedRpcType::UInt64>(definition));
        InitializeDoubleValues(MakePropertyVector<ScriptDefinedRpcType::Double>(definition));
        InitializeVector2Values(MakePropertyVector<ScriptDefinedRpcType::Vector2>(definition));
        InitializeQuaternionValues(MakePropertyVector<ScriptDefinedRpcType::Quaternion>(definition));
        InitializeStringValues(MakePropertyVector<ScriptDefinedRpcType::String>(definition));
        InitializeEntityValues(MakePropertyVector<ScriptDefinedRpcType::Entity>(definition));
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
        const auto* field = m_definition.FindProperty(id);
        if (!m_hasDefinition || !field)
        {
            return false;
        }
        const auto slot = m_definition.GetPropertySlot(id);
        switch (field->m_type)
        {
        case ScriptDefinedRpcType::Boolean:
            if (slot >= BoolValuesGetSize())
            {
                return false;
            }
            value = GetBoolValues(slot);
            break;
        case ScriptDefinedRpcType::Int32:
            if (slot >= Int32ValuesGetSize())
            {
                return false;
            }
            value = GetInt32Values(slot);
            break;
        case ScriptDefinedRpcType::Float:
            if (slot >= ValuesGetSize())
            {
                return false;
            }
            value = GetValues(slot);
            break;
        case ScriptDefinedRpcType::Vector3:
            if (slot >= Vector3ValuesGetSize())
            {
                return false;
            }
            value = GetVector3Values(slot);
            break;
        case ScriptDefinedRpcType::Int8:
            if (slot >= Int8ValuesGetSize())
            {
                return false;
            }
            value = GetInt8Values(slot);
            break;
        case ScriptDefinedRpcType::UInt8:
            if (slot >= UInt8ValuesGetSize())
            {
                return false;
            }
            value = GetUInt8Values(slot);
            break;
        case ScriptDefinedRpcType::Int16:
            if (slot >= Int16ValuesGetSize())
            {
                return false;
            }
            value = GetInt16Values(slot);
            break;
        case ScriptDefinedRpcType::UInt16:
            if (slot >= UInt16ValuesGetSize())
            {
                return false;
            }
            value = GetUInt16Values(slot);
            break;
        case ScriptDefinedRpcType::UInt32:
            if (slot >= UInt32ValuesGetSize())
            {
                return false;
            }
            value = GetUInt32Values(slot);
            break;
        case ScriptDefinedRpcType::Int64:
            if (slot >= Int64ValuesGetSize())
            {
                return false;
            }
            value = GetInt64Values(slot);
            break;
        case ScriptDefinedRpcType::UInt64:
            if (slot >= UInt64ValuesGetSize())
            {
                return false;
            }
            value = GetUInt64Values(slot);
            break;
        case ScriptDefinedRpcType::Double:
            if (slot >= DoubleValuesGetSize())
            {
                return false;
            }
            value = GetDoubleValues(slot);
            break;
        case ScriptDefinedRpcType::Vector2:
            if (slot >= Vector2ValuesGetSize())
            {
                return false;
            }
            value = GetVector2Values(slot);
            break;
        case ScriptDefinedRpcType::Quaternion:
            if (slot >= QuaternionValuesGetSize())
            {
                return false;
            }
            value = GetQuaternionValues(slot);
            break;
        case ScriptDefinedRpcType::String:
            if (slot >= StringValuesGetSize())
            {
                return false;
            }
            value = GetStringValues(slot);
            break;
        case ScriptDefinedRpcType::Entity:
            if (slot >= EntityValuesGetSize())
            {
                return false;
            }
            value = GetEntityValues(slot);
            break;
        default:
            return false;
        }
        return true;
    }
    bool MultiplayerScriptDefinedComponent::WriteProperty(const AZ::Uuid& id, const ScriptDefinedRpcValue& value)
    {
        const auto* field = m_definition.FindProperty(id);
        if (!m_hasDefinition || !field || value.index() != static_cast<size_t>(field->m_type) || !GetController() ||
            !(IsNetEntityRoleAuthority() || (field->m_predictable && IsNetEntityRoleAutonomous())))
        {
            return false;
        }
        ScriptDefinedRpcPayload check;
        check.m_arguments.push_back(value);
        if (!check.IsValid())
        {
            return false;
        }
        const auto slot = m_definition.GetPropertySlot(id);
        auto* controller = static_cast<MultiplayerScriptDefinedComponentController*>(GetController());
        ScriptDefinedRpcValue previous;
        if (!ReadProperty(id, previous))
        {
            return false;
        }
        switch (field->m_type)
        {
        case ScriptDefinedRpcType::Boolean:
            controller->SetBoolValues(slot, AZStd::get<0>(value));
            break;
        case ScriptDefinedRpcType::Int32:
            controller->SetInt32Values(slot, AZStd::get<1>(value));
            break;
        case ScriptDefinedRpcType::Float:
            controller->SetValues(slot, AZStd::get<2>(value));
            break;
        case ScriptDefinedRpcType::Vector3:
            controller->SetVector3Values(slot, AZStd::get<3>(value));
            break;
        case ScriptDefinedRpcType::Int8:
            controller->SetInt8Values(slot, AZStd::get<4>(value));
            break;
        case ScriptDefinedRpcType::UInt8:
            controller->SetUInt8Values(slot, AZStd::get<5>(value));
            break;
        case ScriptDefinedRpcType::Int16:
            controller->SetInt16Values(slot, AZStd::get<6>(value));
            break;
        case ScriptDefinedRpcType::UInt16:
            controller->SetUInt16Values(slot, AZStd::get<7>(value));
            break;
        case ScriptDefinedRpcType::UInt32:
            controller->SetUInt32Values(slot, AZStd::get<8>(value));
            break;
        case ScriptDefinedRpcType::Int64:
            controller->SetInt64Values(slot, AZStd::get<9>(value));
            break;
        case ScriptDefinedRpcType::UInt64:
            controller->SetUInt64Values(slot, AZStd::get<10>(value));
            break;
        case ScriptDefinedRpcType::Double:
            controller->SetDoubleValues(slot, AZStd::get<11>(value));
            break;
        case ScriptDefinedRpcType::Vector2:
            controller->SetVector2Values(slot, AZStd::get<12>(value));
            break;
        case ScriptDefinedRpcType::Quaternion:
            controller->SetQuaternionValues(slot, AZStd::get<13>(value));
            break;
        case ScriptDefinedRpcType::String:
            controller->SetStringValues(slot, AZStd::get<14>(value));
            break;
        case ScriptDefinedRpcType::Entity:
            controller->SetEntityValues(slot, AZStd::get<15>(value));
            break;
        default:
            return false;
        }
        if (previous != value)
        {
            ScriptDefinedNotificationBus::Event(GetEntityId(), &ScriptDefinedNotifications::OnPropertyChanged, id, value);
        }
        return true;
    }
    bool MultiplayerScriptDefinedComponent::ReadArchetype(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const
    {
        const auto* field = m_definition.FindArchetype(id);
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
        if (!m_schemaFingerprint || payload.m_schemaFingerprint != m_schemaFingerprint || !payload.IsValid())
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
#if AZ_TRAIT_SERVER
            if (signature.m_delivery == RpcDeliveryType::AuthorityToClient && signature.m_isReliable == true &&
                IsNetEntityRoleAuthority() && controller)
            {
                controller->Dispatch(payload);
                return true;
            }
#endif
#if AZ_TRAIT_SERVER
            if (signature.m_delivery == RpcDeliveryType::AuthorityToClient && signature.m_isReliable == false &&
                IsNetEntityRoleAuthority() && controller)
            {
                controller->DispatchAuthorityToClientUnreliable(payload);
                return true;
            }
#endif
#if AZ_TRAIT_CLIENT
            if (signature.m_delivery == RpcDeliveryType::AutonomousToAuthority && signature.m_isReliable == true &&
                IsNetEntityRoleAutonomous() && controller)
            {
                controller->DispatchAutonomousToAuthority(payload);
                return true;
            }
#endif
#if AZ_TRAIT_CLIENT
            if (signature.m_delivery == RpcDeliveryType::AutonomousToAuthority && signature.m_isReliable == false &&
                IsNetEntityRoleAutonomous() && controller)
            {
                controller->DispatchAutonomousToAuthorityUnreliable(payload);
                return true;
            }
#endif
#if AZ_TRAIT_SERVER
            if (signature.m_delivery == RpcDeliveryType::AuthorityToAutonomous && signature.m_isReliable == true &&
                IsNetEntityRoleAuthority() && controller)
            {
                controller->DispatchAuthorityToAutonomous(payload);
                return true;
            }
#endif
#if AZ_TRAIT_SERVER
            if (signature.m_delivery == RpcDeliveryType::AuthorityToAutonomous && signature.m_isReliable == false &&
                IsNetEntityRoleAuthority() && controller)
            {
                controller->DispatchAuthorityToAutonomousUnreliable(payload);
                return true;
            }
#endif
#if AZ_TRAIT_SERVER
            if (signature.m_delivery == RpcDeliveryType::ServerToAuthority && signature.m_isReliable == true &&
                (IsNetEntityRoleServer() || IsNetEntityRoleAuthority()))
            {
                DispatchServerToAuthority(payload);
                return true;
            }
#endif
#if AZ_TRAIT_SERVER
            if (signature.m_delivery == RpcDeliveryType::ServerToAuthority && signature.m_isReliable == false &&
                (IsNetEntityRoleServer() || IsNetEntityRoleAuthority()))
            {
                DispatchServerToAuthorityUnreliable(payload);
                return true;
            }
#endif
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
    void MultiplayerScriptDefinedComponent::HandleDispatch(AzNetworking::IConnection* connection, const ScriptDefinedRpcPayload& payload)
    {
        ReceiveRpc(connection, payload, RpcDeliveryType::AuthorityToClient, true);
    }
#endif
#if AZ_TRAIT_CLIENT
    void MultiplayerScriptDefinedComponent::HandleDispatchAuthorityToClientUnreliable(
        AzNetworking::IConnection* connection, const ScriptDefinedRpcPayload& payload)
    {
        ReceiveRpc(connection, payload, RpcDeliveryType::AuthorityToClient, false);
    }
#endif
#if AZ_TRAIT_SERVER
    void MultiplayerScriptDefinedComponentController::HandleDispatchAutonomousToAuthority(
        AzNetworking::IConnection* connection, const ScriptDefinedRpcPayload& payload)
    {
        GetParent().ReceiveRpc(connection, payload, RpcDeliveryType::AutonomousToAuthority, true);
    }
#endif
#if AZ_TRAIT_SERVER
    void MultiplayerScriptDefinedComponentController::HandleDispatchAutonomousToAuthorityUnreliable(
        AzNetworking::IConnection* connection, const ScriptDefinedRpcPayload& payload)
    {
        GetParent().ReceiveRpc(connection, payload, RpcDeliveryType::AutonomousToAuthority, false);
    }
#endif
#if AZ_TRAIT_CLIENT
    void MultiplayerScriptDefinedComponentController::HandleDispatchAuthorityToAutonomous(
        AzNetworking::IConnection* connection, const ScriptDefinedRpcPayload& payload)
    {
        GetParent().ReceiveRpc(connection, payload, RpcDeliveryType::AuthorityToAutonomous, true);
    }
#endif
#if AZ_TRAIT_CLIENT
    void MultiplayerScriptDefinedComponentController::HandleDispatchAuthorityToAutonomousUnreliable(
        AzNetworking::IConnection* connection, const ScriptDefinedRpcPayload& payload)
    {
        GetParent().ReceiveRpc(connection, payload, RpcDeliveryType::AuthorityToAutonomous, false);
    }
#endif
#if AZ_TRAIT_SERVER
    void MultiplayerScriptDefinedComponentController::HandleDispatchServerToAuthority(
        AzNetworking::IConnection* connection, const ScriptDefinedRpcPayload& payload)
    {
        GetParent().ReceiveRpc(connection, payload, RpcDeliveryType::ServerToAuthority, true);
    }
#endif
#if AZ_TRAIT_SERVER
    void MultiplayerScriptDefinedComponentController::HandleDispatchServerToAuthorityUnreliable(
        AzNetworking::IConnection* connection, const ScriptDefinedRpcPayload& payload)
    {
        GetParent().ReceiveRpc(connection, payload, RpcDeliveryType::ServerToAuthority, false);
    }
#endif
    void MultiplayerScriptDefinedComponent::AddRpcReceivedHandler(RpcReceivedEvent::Handler& handler)
    {
        handler.Connect(m_rpcReceivedEvent);
    }

    void MultiplayerScriptDefinedComponent::NetworkAttach(
        NetBindComponent* binding, ReplicationRecord& current, ReplicationRecord& predictable)
    {
        if (!IsValidRpcSchema(m_schemaFingerprint, m_rpcSignatures) ||
            (m_hasDefinition && (!m_definition.Validate() || m_definition.GetFingerprint() != m_schemaFingerprint)))
        {
            m_hasDefinition = false;
            m_schemaFingerprint = 0;
            m_rpcSignatures.clear();
        }
        // Hidden network banks are not serialized by the generated base. Rebuild
        // their sizes/defaults from the persisted definition before attachment
        // registers dirty bits or a first snapshot applies replicated values.
        if (m_hasDefinition && !m_configurationLocked)
        {
            InitializeProperties(m_definition);
        }
        m_configurationLocked = true;
        const auto start = current.m_authorityToClient.GetValidBitCount();
        MultiplayerScriptDefinedComponentBase::NetworkAttach(binding, current, predictable);
        if (m_hasDefinition)
        {
            // The compiled banks support prediction, but only authored fields
            // marked predictable belong in input hashes/corrections. Keep the
            // size bit when a bank contains at least one predicted element.
            for (uint32_t bit = 0; bit < ScriptDefinedPropertyRecordBits; ++bit)
            {
                predictable.m_authorityToClient.SetBit(start + bit, false);
            }
            for (const auto& field : m_definition.m_properties)
            {
                if (field.m_predictable)
                {
                    const auto bit = PropertyBit(field, m_definition);
                    predictable.m_authorityToClient.SetBit(start + bit, true);
                    predictable.m_authorityToClient.SetBit(
                        start + bit - m_definition.GetPropertySlot(field.m_id) + ScriptDefinedPropertyCapacity, true);
                }
            }
        }
#if AZ_TRAIT_SERVER
        if (m_hasDefinition && IsNetEntityRoleAuthority())
        {
            // Attachment precedes controller construction. Mark only populated slots and their size fields.
            for (const auto& field : m_definition.m_properties)
            {
                const auto bit = PropertyBit(field, m_definition);
                current.m_authorityToClient.SetBit(start + bit, true);
                current.m_authorityToClient.SetBit(
                    start + bit - m_definition.GetPropertySlot(field.m_id) + ScriptDefinedPropertyCapacity, true);
            }
        }
#else
        AZ_UNUSED(start);
#endif
    }

    bool MultiplayerScriptDefinedComponent::SerializeStateDeltaMessage(ReplicationRecord& record, AzNetworking::ISerializer& serializer)
    {
        bool changed = false;
        if (record.ContainsAuthorityToClientBits())
        {
            if (record.GetRemainingAuthorityToClientBits() < ScriptDefinedPropertyRecordBits)
            {
                serializer.Invalidate();
                return false;
            }
            for (uint32_t bit = 0; bit < ScriptDefinedPropertyRecordBits; ++bit)
            {
                changed |= record.m_authorityToClient.GetBit(record.m_authorityToClientConsumedBits + bit);
            }
        }
        if (changed)
        {
            AZ::u64 fingerprint = m_schemaFingerprint;
            if (!serializer.Serialize(fingerprint, "ScriptSchema") || fingerprint != m_schemaFingerprint)
            {
                serializer.Invalidate();
                return false;
            }
        }
        return MultiplayerScriptDefinedComponentBase::SerializeStateDeltaMessage(record, serializer);
    }
    void MultiplayerScriptDefinedComponent::NotifyStateDeltaChanges(ReplicationRecord& record)
    {
        const auto start = record.m_authorityToClientConsumedBits;
        const bool hasBits = record.ContainsAuthorityToClientBits();
        MultiplayerScriptDefinedComponentBase::NotifyStateDeltaChanges(record);
        if (m_hasDefinition && hasBits)
        {
            for (const auto& field : m_definition.m_properties)
            {
                if (record.m_authorityToClient.GetBit(start + PropertyBit(field, m_definition)))
                {
                    ScriptDefinedRpcValue value;
                    if (ReadProperty(field.m_id, value))
                    {
                        ScriptDefinedNotificationBus::Event(
                            GetEntityId(), &ScriptDefinedNotifications::OnPropertyChanged, field.m_id, value);
                    }
                }
            }
        }
    }
    bool MultiplayerScriptDefinedComponentController::TrySetValue(uint32_t index, float value)
    {
        if (IsNetEntityRoleAuthority() && index < ValuesGetSize() && std::isfinite(value))
        {
            SetValues(index, value);
            return true;
        }
        return false;
    }
} // namespace Multiplayer
