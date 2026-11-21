/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Matrix3x3.h>
#include <AzCore/Math/Matrix4x4.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector2.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Math/Vector4.h>
#include <AzCore/std/containers/fixed_vector.h>
#include <AzCore/std/containers/variant.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/fixed_string.h>
#include <AzCore/std/utils.h>
#include <AzNetworking/Serialization/ISerializer.h>
#include <Multiplayer/NetworkEntity/NetworkEntityRpcMessage.h>

//! Lists every script-defined value type once, as X(EnumName, CppType). ScriptDefinedRpcType, ScriptDefinedRpcValue, the
//! reflected type names, property bank access and the Script Canvas pin types all derive from this list. Each entry also
//! needs an {EnumName}Values bank in MultiplayerScriptDefinedComponent.AutoComponent.xml; a missing bank fails to compile.
//! Order: Boolean; signed then unsigned integers by width; floating point; vectors and quaternion; transform and matrices;
//! String; Entity. The enum values follow this order.
#define MULTIPLAYER_SCRIPT_DEFINED_TYPES(X)                                                                                                \
    X(Boolean, bool)                                                                                                                       \
    X(Int8, int8_t)                                                                                                                        \
    X(Int16, int16_t)                                                                                                                      \
    X(Int32, int32_t)                                                                                                                      \
    X(Int64, int64_t)                                                                                                                      \
    X(UInt8, uint8_t)                                                                                                                      \
    X(UInt16, uint16_t)                                                                                                                    \
    X(UInt32, uint32_t)                                                                                                                    \
    X(UInt64, uint64_t)                                                                                                                    \
    X(Float, float)                                                                                                                        \
    X(Double, double)                                                                                                                      \
    X(Vector2, AZ::Vector2)                                                                                                                \
    X(Vector3, AZ::Vector3)                                                                                                                \
    X(Vector4, AZ::Vector4)                                                                                                                \
    X(Quaternion, AZ::Quaternion)                                                                                                          \
    X(Transform, AZ::Transform)                                                                                                            \
    X(Matrix3x3, AZ::Matrix3x3)                                                                                                            \
    X(Matrix4x4, AZ::Matrix4x4)                                                                                                            \
    X(String, Multiplayer::ScriptDefinedString)                                                                                            \
    X(Entity, Multiplayer::NetEntityId)

namespace Multiplayer
{
    //! Number of elements in each typed property bank. A schema can declare this many non-rewindable properties of each type.
    inline constexpr uint32_t ScriptDefinedPropertyCapacity = 16;

    //! Replication record bits used by one typed bank: one bit per element plus the bank's size bit.
    inline constexpr uint32_t ScriptDefinedPropertyBankBits = ScriptDefinedPropertyCapacity + 1;

    //! Number of rewindable properties a schema can declare, across all value types. They share one bank whose elements
    //! each keep a RewindHistorySize-frame history, which is why this capacity is small.
    inline constexpr uint32_t ScriptDefinedRewindableCapacity = 4;

    //! Maximum number of fields in one input frame.
    inline constexpr uint32_t ScriptDefinedInputCapacity = 8;

    //! Maximum number of arguments of one RPC.
    inline constexpr uint32_t ScriptDefinedArgumentCapacity = 8;

    //! Maximum number of RPCs in one schema.
    inline constexpr uint32_t ScriptDefinedRpcCapacity = 16;

    //! Maximum length in bytes of a replicated string value.
    inline constexpr size_t ScriptDefinedStringCapacity = 64;

    //! String value type; fixed size so that it can be replicated in a property bank.
    using ScriptDefinedString = AZStd::fixed_string<ScriptDefinedStringCapacity>;

    //! Value types of schema fields and RPC parameters, in MULTIPLAYER_SCRIPT_DEFINED_TYPES order.
    enum class ScriptDefinedRpcType : uint8_t
    {
#define MULTIPLAYER_SCRIPT_DEFINED_ENUMERATOR(EnumName, CppType) EnumName,
        MULTIPLAYER_SCRIPT_DEFINED_TYPES(MULTIPLAYER_SCRIPT_DEFINED_ENUMERATOR)
#undef MULTIPLAYER_SCRIPT_DEFINED_ENUMERATOR
    };

    AZ_TYPE_INFO_SPECIALIZE(ScriptDefinedRpcType, "{A12C7B97-2A94-4FC4-98CA-14F467A89C26}");

    //! Number of script-defined value types.
    inline constexpr uint32_t ScriptDefinedTypeCount = 0
#define MULTIPLAYER_SCRIPT_DEFINED_COUNT(EnumName, CppType) +1
        MULTIPLAYER_SCRIPT_DEFINED_TYPES(MULTIPLAYER_SCRIPT_DEFINED_COUNT)
#undef MULTIPLAYER_SCRIPT_DEFINED_COUNT
        ;

    //! Number of typed property banks: one per value type.
    inline constexpr uint32_t ScriptDefinedPropertyBankCount = ScriptDefinedTypeCount;

    //! Replication record bits used by all property banks of a script-defined component: the typed banks, plus the
    //! rewindable bank, which has one bit per element and no size bit.
    inline constexpr uint32_t ScriptDefinedPropertyRecordBits =
        ScriptDefinedPropertyBankCount * ScriptDefinedPropertyBankBits + ScriptDefinedRewindableCapacity;

    //! Maps a ScriptDefinedRpcType to its C++ type.
    template<ScriptDefinedRpcType Type>
    struct ScriptDefinedTypeTraits;

#define MULTIPLAYER_SCRIPT_DEFINED_TRAITS(EnumName, CppType)                                                                               \
    template<>                                                                                                                             \
    struct ScriptDefinedTypeTraits<ScriptDefinedRpcType::EnumName>                                                                         \
    {                                                                                                                                      \
        using ValueType = CppType;                                                                                                         \
    };
    MULTIPLAYER_SCRIPT_DEFINED_TYPES(MULTIPLAYER_SCRIPT_DEFINED_TRAITS)
#undef MULTIPLAYER_SCRIPT_DEFINED_TRAITS

    //! The C++ type a ScriptDefinedRpcValue holds for the given ScriptDefinedRpcType.
    template<ScriptDefinedRpcType Type>
    using ScriptDefinedValueType = typename ScriptDefinedTypeTraits<Type>::ValueType;

    namespace Internal
    {
        //! Declares the variant whose alternative at each index is the C++ type of the ScriptDefinedRpcType with that value.
        template<size_t... TypeIndices>
        auto MakeScriptDefinedVariant(AZStd::index_sequence<TypeIndices...>)
            -> AZStd::variant<ScriptDefinedValueType<static_cast<ScriptDefinedRpcType>(TypeIndices)>...>;
    } // namespace Internal

    //! A value of any script-defined type. The alternative index is the ScriptDefinedRpcType of the value.
    using ScriptDefinedRpcValue = decltype(Internal::MakeScriptDefinedVariant(AZStd::make_index_sequence<ScriptDefinedTypeCount>{}));

    //! Wire signature of one RPC, used to check received payloads before they are dispatched.
    struct ScriptDefinedRpcSignature
    {
        AZ_TYPE_INFO(ScriptDefinedRpcSignature, "{F8790DE9-441C-4B3A-B95C-157BDDE2CD61}");
        static void Reflect(AZ::ReflectContext* context);

        uint16_t m_rpcId = 0;
        AZStd::fixed_vector<ScriptDefinedRpcType, ScriptDefinedArgumentCapacity> m_parameterTypes;
        RpcDeliveryType m_delivery = RpcDeliveryType::AuthorityToClient;
        bool m_isReliable = true;
    };

    //! Parameters of a script-defined RPC as they travel on the wire.
    struct ScriptDefinedRpcPayload : Multiplayer::IRpcParamStruct
    {
        //! Checks only the wire bounds; the receiver checks the schema and signature before dispatch.
        //! The payload is left unchanged when reading fails.
        bool Serialize(AzNetworking::ISerializer& serializer) override;

        //! Returns true if the RPC ID and the argument types match the signature.
        bool MatchesSignature(const ScriptDefinedRpcSignature& signature) const;

        //! Returns true if every argument holds a finite value.
        bool IsValid() const;

        AZ::u64 m_schemaFingerprint = 0;
        uint16_t m_rpcId = 0;
        AZStd::fixed_vector<ScriptDefinedRpcValue, ScriptDefinedArgumentCapacity> m_arguments;
    };

    //! One frame of script-defined input.
    //! Authored schemas use typed fields. The untyped float layout remains available to native callers.
    struct ScriptDefinedInputFrame
    {
        //! Serializes the frame. The frame is left unchanged when reading fails.
        bool Serialize(AzNetworking::ISerializer& serializer);

        //! Returns true if the frame is within capacity and every value is finite.
        bool IsValid() const;

        bool operator==(const ScriptDefinedInputFrame& rhs) const;
        bool operator!=(const ScriptDefinedInputFrame& rhs) const
        {
            return !(*this == rhs);
        }

        AZ::u64 m_schemaFingerprint = 0;
        AZStd::fixed_vector<float, ScriptDefinedInputCapacity> m_values;
        AZStd::vector<ScriptDefinedRpcValue> m_typedValues;
        bool m_isTyped = false;
    };

    //! Returns true if the value holds no infinity or NaN.
    bool IsFiniteScriptDefinedValue(const ScriptDefinedRpcValue& value);

    //! Serializes one value with its type tag. Invalidates the serializer on an unknown type or a non-finite value.
    bool SerializeScriptDefinedValue(AzNetworking::ISerializer& serializer, ScriptDefinedRpcValue& value);
} // namespace Multiplayer

namespace AzNetworking
{
    //! Lets serializers, and the rewindable property bank, read and write a script-defined value with its type tag.
    template<>
    struct SerializeObjectHelper<Multiplayer::ScriptDefinedRpcValue>
    {
        static bool SerializeObject(ISerializer& serializer, Multiplayer::ScriptDefinedRpcValue& value)
        {
            return Multiplayer::SerializeScriptDefinedValue(serializer, value);
        }
    };
} // namespace AzNetworking
