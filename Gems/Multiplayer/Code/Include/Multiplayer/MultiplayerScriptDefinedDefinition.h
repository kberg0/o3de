/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/EBus/EBus.h>
#include <AzCore/Outcome/Outcome.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>
#include <Multiplayer/MultiplayerScriptDefinedTypes.h>

namespace Multiplayer
{
    struct ScriptDefinedParameter
    {
        AZ_TYPE_INFO(ScriptDefinedParameter, "{E37923D3-842F-4434-9317-31BC0FD5E739}");
        static void Reflect(AZ::ReflectContext* context);
        AZStd::string GetLabel() const
        {
            return m_name;
        }
        AZ::Uuid m_id = AZ::Uuid::CreateRandom();
        AZStd::string m_name = "Field";
        ScriptDefinedRpcType m_type = ScriptDefinedRpcType::Float;
    };

    struct ScriptDefinedField : ScriptDefinedParameter
    {
        AZ_TYPE_INFO(ScriptDefinedField, "{2B52CF1B-C53F-4078-9CDD-D2A078A49163}");
        static void Reflect(AZ::ReflectContext* context);
        AZStd::string GetLabel() const
        {
            return m_name;
        }
        AZStd::string m_defaultValue;
        bool m_predictable = false;
    };

    struct ScriptDefinedRpcDefinition
    {
        AZ_TYPE_INFO(ScriptDefinedRpcDefinition, "{E5D8B7C1-A384-4F89-AFBC-2750D6DBBF09}");
        static void Reflect(AZ::ReflectContext* context);
        AZStd::string GetLabel() const
        {
            return m_name;
        }
        AZ::Uuid m_id = AZ::Uuid::CreateRandom();
        AZStd::string m_name = "Rpc";
        RpcDeliveryType m_delivery = RpcDeliveryType::AuthorityToClient;
        bool m_isReliable = true;
        AZStd::vector<ScriptDefinedParameter> m_parameters;
    };

    struct MultiplayerScriptDefinedDefinition
    {
        AZ_TYPE_INFO(MultiplayerScriptDefinedDefinition, "{14ED7206-C56D-4C66-8189-E4599B07D878}");
        static void Reflect(AZ::ReflectContext* context);
        AZ::Outcome<void, AZStd::string> Validate() const;
        AZ::u64 GetFingerprint() const;
        AZStd::vector<ScriptDefinedRpcSignature> GetRpcSignatures() const;
        uint16_t GetRpcId(const AZ::Uuid& id) const;
        uint32_t GetPropertySlot(const AZ::Uuid& id) const;
        const ScriptDefinedField* FindProperty(const AZ::Uuid& id) const;
        const ScriptDefinedField* FindArchetype(const AZ::Uuid& id) const;
        const ScriptDefinedRpcDefinition* FindRpc(const AZ::Uuid& id) const;

        AZ::Uuid m_id = AZ::Uuid::CreateRandom();
        AZStd::string m_name = "MultiplayerComponent";
        AZ::u32 m_revision = 1;
        AZStd::vector<ScriptDefinedField> m_properties;
        AZStd::vector<ScriptDefinedField> m_inputs;
        AZStd::vector<ScriptDefinedField> m_archetypes;
        AZStd::vector<ScriptDefinedRpcDefinition> m_rpcs;
    };

    class MultiplayerScriptDefinedAsset : public AZ::Data::AssetData
    {
    public:
        AZ_RTTI(MultiplayerScriptDefinedAsset, "{3F4C9B2E-1E51-4641-B5AB-DC1632718C9A}", AZ::Data::AssetData);
        AZ_CLASS_ALLOCATOR(MultiplayerScriptDefinedAsset, AZ::SystemAllocator);
        explicit MultiplayerScriptDefinedAsset(const AZ::Data::AssetId& id = {}, AssetStatus status = AssetStatus::NotLoaded)
            : AZ::Data::AssetData(id, status)
        {
        }
        static void Reflect(AZ::ReflectContext* context);
        MultiplayerScriptDefinedDefinition m_definition;
        AZ::u32 ValidateForEditor();
        AZStd::string m_validation = "Valid";
    };

    inline constexpr uint32_t ScriptDefinedInterfaceSubId = 1;

    ScriptDefinedRpcValue DefaultScriptDefinedValue(ScriptDefinedRpcType type);
    AZ::Outcome<ScriptDefinedRpcValue, AZStd::string> ParseScriptDefinedValue(ScriptDefinedRpcType type, AZStd::string_view text);
    AZStd::string FormatScriptDefinedValue(const ScriptDefinedRpcValue& value);
    bool IsScriptDefinedIdentifier(AZStd::string_view name);

    class ScriptDefinedRequests : public AZ::EBusTraits
    {
    public:
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        using BusIdType = AZ::EntityId;
        virtual const MultiplayerScriptDefinedDefinition* GetDefinition() const = 0;
        virtual bool ReadProperty(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const = 0;
        virtual bool WriteProperty(const AZ::Uuid& id, const ScriptDefinedRpcValue& value) = 0;
        virtual bool ReadArchetype(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const = 0;
        virtual bool InvokeRpc(
            const AZ::Uuid& id, const AZStd::fixed_vector<ScriptDefinedRpcValue, ScriptDefinedArgumentCapacity>& arguments) = 0;
    };
    using ScriptDefinedRequestBus = AZ::EBus<ScriptDefinedRequests>;

    class ScriptDefinedInputRequests : public AZ::EBusTraits
    {
    public:
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        using BusIdType = AZ::EntityId;
        virtual bool ReadInput(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const = 0;
        virtual bool WriteInput(const AZ::Uuid& id, const ScriptDefinedRpcValue& value) = 0;
    };
    using ScriptDefinedInputRequestBus = AZ::EBus<ScriptDefinedInputRequests>;

    class ScriptDefinedNotifications : public AZ::EBusTraits
    {
    public:
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        using BusIdType = AZ::EntityId;
        virtual void OnPropertyChanged(const AZ::Uuid&, const ScriptDefinedRpcValue&)
        {
        }
        virtual void OnRpcReceived(uint16_t, const ScriptDefinedRpcPayload&)
        {
        }
        virtual void OnCreateInput(float)
        {
        }
        virtual void OnProcessInput(float, bool)
        {
        }
    };
    using ScriptDefinedNotificationBus = AZ::EBus<ScriptDefinedNotifications>;
} // namespace Multiplayer
