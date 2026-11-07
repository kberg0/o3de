/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Vector2.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/fixed_vector.h>
#include <AzCore/std/containers/variant.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/fixed_string.h>
#include <Multiplayer/NetworkEntity/NetworkEntityRpcMessage.h>

namespace Multiplayer
{
    inline constexpr uint32_t ScriptDefinedPropertyCapacity = 16;
    inline constexpr uint32_t ScriptDefinedPropertyBankCount = 16;
    inline constexpr uint32_t ScriptDefinedPropertyRecordBits = ScriptDefinedPropertyBankCount * (ScriptDefinedPropertyCapacity + 1);
    inline constexpr uint32_t ScriptDefinedInputCapacity = 8;
    inline constexpr uint32_t ScriptDefinedArgumentCapacity = 8;
    inline constexpr uint32_t ScriptDefinedRpcCapacity = 16;

    using ScriptDefinedString = AZStd::fixed_string<64>;
    using ScriptDefinedRpcValue = AZStd::variant<
        bool,
        int32_t,
        float,
        AZ::Vector3,
        int8_t,
        uint8_t,
        int16_t,
        uint16_t,
        uint32_t,
        int64_t,
        uint64_t,
        double,
        AZ::Vector2,
        AZ::Quaternion,
        ScriptDefinedString,
        NetEntityId>;

    enum class ScriptDefinedRpcType : uint8_t
    {
        Boolean = 0,
        Int32 = 1,
        Float = 2,
        Vector3 = 3,
        Int8 = 4,
        UInt8 = 5,
        Int16 = 6,
        UInt16 = 7,
        UInt32 = 8,
        Int64 = 9,
        UInt64 = 10,
        Double = 11,
        Vector2 = 12,
        Quaternion = 13,
        String = 14,
        Entity = 15
    };

    AZ_TYPE_INFO_SPECIALIZE(ScriptDefinedRpcType, "{A12C7B97-2A94-4FC4-98CA-14F467A89C26}");

    static_assert(
        AZStd::is_same_v<AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::Boolean), ScriptDefinedRpcValue>, bool>);
    static_assert(
        AZStd::is_same_v<AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::Int32), ScriptDefinedRpcValue>, int32_t>);
    static_assert(
        AZStd::is_same_v<AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::Float), ScriptDefinedRpcValue>, float>);
    static_assert(AZStd::is_same_v<
                  AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::Vector3), ScriptDefinedRpcValue>,
                  AZ::Vector3>);
    static_assert(
        AZStd::is_same_v<AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::Int8), ScriptDefinedRpcValue>, int8_t>);
    static_assert(
        AZStd::is_same_v<AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::UInt8), ScriptDefinedRpcValue>, uint8_t>);
    static_assert(
        AZStd::is_same_v<AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::Int16), ScriptDefinedRpcValue>, int16_t>);
    static_assert(
        AZStd::is_same_v<AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::UInt16), ScriptDefinedRpcValue>, uint16_t>);
    static_assert(
        AZStd::is_same_v<AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::UInt32), ScriptDefinedRpcValue>, uint32_t>);
    static_assert(
        AZStd::is_same_v<AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::Int64), ScriptDefinedRpcValue>, int64_t>);
    static_assert(
        AZStd::is_same_v<AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::UInt64), ScriptDefinedRpcValue>, uint64_t>);
    static_assert(
        AZStd::is_same_v<AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::Double), ScriptDefinedRpcValue>, double>);
    static_assert(AZStd::is_same_v<
                  AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::Vector2), ScriptDefinedRpcValue>,
                  AZ::Vector2>);
    static_assert(AZStd::is_same_v<
                  AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::Quaternion), ScriptDefinedRpcValue>,
                  AZ::Quaternion>);
    static_assert(AZStd::is_same_v<
                  AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::String), ScriptDefinedRpcValue>,
                  ScriptDefinedString>);
    static_assert(AZStd::is_same_v<
                  AZStd::variant_alternative_t<static_cast<size_t>(ScriptDefinedRpcType::Entity), ScriptDefinedRpcValue>,
                  NetEntityId>);

    struct ScriptDefinedRpcSignature
    {
        AZ_TYPE_INFO(ScriptDefinedRpcSignature, "{F8790DE9-441C-4B3A-B95C-157BDDE2CD61}");
        static void Reflect(AZ::ReflectContext* context);

        uint16_t m_rpcId = 0;
        AZStd::fixed_vector<ScriptDefinedRpcType, ScriptDefinedArgumentCapacity> m_parameterTypes;
        RpcDeliveryType m_delivery = RpcDeliveryType::AuthorityToClient;
        bool m_isReliable = true;
    };

    struct ScriptDefinedRpcPayload : Multiplayer::IRpcParamStruct
    {
        AZ::u64 m_schemaFingerprint = 0;
        uint16_t m_rpcId = 0;
        AZStd::fixed_vector<ScriptDefinedRpcValue, ScriptDefinedArgumentCapacity> m_arguments;

        //! Wire bounds are checked here; the receiver checks the schema/signature before dispatch.
        bool Serialize(AzNetworking::ISerializer& serializer) override;
        bool MatchesSignature(const ScriptDefinedRpcSignature& signature) const;
        bool IsValid() const;
    };

    struct ScriptDefinedInputFrame
    {
        AZ::u64 m_schemaFingerprint = 0;
        AZStd::fixed_vector<float, ScriptDefinedInputCapacity> m_values;
        //! Authored schemas use typed fields. The float layout remains available to existing native callers.
        AZStd::vector<ScriptDefinedRpcValue> m_typedValues;
        bool m_isTyped = false;

        bool Serialize(AzNetworking::ISerializer& serializer);
        bool IsValid() const;
        bool operator==(const ScriptDefinedInputFrame& rhs) const;
        bool operator!=(const ScriptDefinedInputFrame& rhs) const
        {
            return !(*this == rhs);
        }
    };

    bool SerializeScriptDefinedValue(AzNetworking::ISerializer& serializer, ScriptDefinedRpcValue& value);
} // namespace Multiplayer
