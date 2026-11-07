/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/Serialization/SerializeContext.h>
#include <Multiplayer/MultiplayerScriptDefinedTypes.h>
#include <cmath>

namespace Multiplayer
{
    namespace
    {
        bool IsValidValue(const ScriptDefinedRpcValue& value)
        {
            return AZStd::visit(
                [](const auto& element)
                {
                    using Type = AZStd::decay_t<decltype(element)>;
                    if constexpr (AZStd::is_same_v<Type, float> || AZStd::is_same_v<Type, double>)
                    {
                        return std::isfinite(element);
                    }
                    else if constexpr (
                        AZStd::is_same_v<Type, AZ::Vector3> || AZStd::is_same_v<Type, AZ::Vector2> ||
                        AZStd::is_same_v<Type, AZ::Quaternion>)
                    {
                        return element.IsFinite();
                    }
                    else
                    {
                        return true;
                    }
                },
                value);
        }

        bool SerializeValue(AzNetworking::ISerializer& serializer, ScriptDefinedRpcValue& value)
        {
            uint8_t tag = aznumeric_cast<uint8_t>(value.index());
            if (!serializer.Serialize(tag, "Type"))
            {
                return false;
            }

            if (serializer.GetSerializerMode() == AzNetworking::SerializerMode::WriteToObject)
            {
                switch (tag)
                {
                case 0:
                    value = false;
                    break;
                case 1:
                    value = int32_t{ 0 };
                    break;
                case 2:
                    value = 0.0f;
                    break;
                case 3:
                    value = AZ::Vector3::CreateZero();
                    break;
                case 4:
                    value = int8_t{ 0 };
                    break;
                case 5:
                    value = uint8_t{ 0 };
                    break;
                case 6:
                    value = int16_t{ 0 };
                    break;
                case 7:
                    value = uint16_t{ 0 };
                    break;
                case 8:
                    value = uint32_t{ 0 };
                    break;
                case 9:
                    value = int64_t{ 0 };
                    break;
                case 10:
                    value = uint64_t{ 0 };
                    break;
                case 11:
                    value = 0.0;
                    break;
                case 12:
                    value = AZ::Vector2::CreateZero();
                    break;
                case 13:
                    value = AZ::Quaternion::CreateIdentity();
                    break;
                case 14:
                    value = ScriptDefinedString{};
                    break;
                case 15:
                    value = InvalidNetEntityId;
                    break;
                default:
                    return false;
                }
            }

            return AZStd::visit(
                       [&serializer](auto& element)
                       {
                           return serializer.Serialize(element, "Value");
                       },
                       value) &&
                IsValidValue(value);
        }
    } // namespace

    bool SerializeScriptDefinedValue(AzNetworking::ISerializer& serializer, ScriptDefinedRpcValue& value)
    {
        if (!SerializeValue(serializer, value))
        {
            serializer.Invalidate();
            return false;
        }
        return true;
    }

    void ScriptDefinedRpcSignature::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ScriptDefinedRpcSignature>()
                ->Version(1)
                ->Field("RpcId", &ScriptDefinedRpcSignature::m_rpcId)
                ->Field("ParameterTypes", &ScriptDefinedRpcSignature::m_parameterTypes)
                ->Field("Delivery", &ScriptDefinedRpcSignature::m_delivery)
                ->Field("Reliable", &ScriptDefinedRpcSignature::m_isReliable);
        }
    }

    bool ScriptDefinedRpcPayload::Serialize(AzNetworking::ISerializer& serializer)
    {
        ScriptDefinedRpcPayload candidate = *this;
        uint8_t count = aznumeric_cast<uint8_t>(candidate.m_arguments.size());
        if (!serializer.Serialize(candidate.m_schemaFingerprint, "Schema") || !serializer.Serialize(candidate.m_rpcId, "Rpc") ||
            !serializer.Serialize(count, "Count") || count > ScriptDefinedArgumentCapacity)
        {
            serializer.Invalidate();
            return false;
        }
        candidate.m_arguments.resize(count);
        for (uint32_t index = 0; index < count; ++index)
        {
            if (!serializer.BeginObject(AzNetworking::GenerateIndexLabel<ScriptDefinedArgumentCapacity>(index).c_str()) ||
                !SerializeValue(serializer, candidate.m_arguments[index]) ||
                !serializer.EndObject(AzNetworking::GenerateIndexLabel<ScriptDefinedArgumentCapacity>(index).c_str()))
            {
                serializer.Invalidate();
                return false;
            }
        }
        if (!serializer.IsValid())
        {
            return false;
        }
        *this = AZStd::move(candidate);
        return true;
    }

    bool ScriptDefinedRpcPayload::MatchesSignature(const ScriptDefinedRpcSignature& signature) const
    {
        if (m_rpcId != signature.m_rpcId || m_arguments.size() != signature.m_parameterTypes.size())
        {
            return false;
        }
        for (size_t index = 0; index < m_arguments.size(); ++index)
        {
            if (m_arguments[index].index() != static_cast<size_t>(signature.m_parameterTypes[index]))
            {
                return false;
            }
        }
        return true;
    }

    bool ScriptDefinedRpcPayload::IsValid() const
    {
        for (const auto& value : m_arguments)
        {
            if (!IsValidValue(value))
            {
                return false;
            }
        }
        return true;
    }

    bool ScriptDefinedInputFrame::Serialize(AzNetworking::ISerializer& serializer)
    {
        if (m_typedValues.size() > ScriptDefinedInputCapacity)
        {
            serializer.Invalidate();
            return false;
        }
        ScriptDefinedInputFrame candidate = *this;
        if (!serializer.Serialize(candidate.m_isTyped, "Typed"))
        {
            serializer.Invalidate();
            return false;
        }
        uint8_t count = aznumeric_cast<uint8_t>(candidate.m_isTyped ? candidate.m_typedValues.size() : candidate.m_values.size());
        if (!serializer.Serialize(candidate.m_schemaFingerprint, "Schema") || !serializer.Serialize(count, "Count") ||
            count > ScriptDefinedInputCapacity)
        {
            serializer.Invalidate();
            return false;
        }
        if (candidate.m_isTyped)
        {
            candidate.m_typedValues.resize(count);
            candidate.m_values.clear();
        }
        else
        {
            candidate.m_values.resize(count);
            candidate.m_typedValues.clear();
        }
        for (uint32_t index = 0; index < count; ++index)
        {
            if (candidate.m_isTyped)
            {
                if (!serializer.BeginObject(AzNetworking::GenerateIndexLabel<ScriptDefinedInputCapacity>(index).c_str()) ||
                    !SerializeScriptDefinedValue(serializer, candidate.m_typedValues[index]) ||
                    !serializer.EndObject(AzNetworking::GenerateIndexLabel<ScriptDefinedInputCapacity>(index).c_str()))
                {
                    serializer.Invalidate();
                    return false;
                }
                continue;
            }
            if (!serializer.Serialize(
                    candidate.m_values[index], AzNetworking::GenerateIndexLabel<ScriptDefinedInputCapacity>(index).c_str()) ||
                !std::isfinite(candidate.m_values[index]))
            {
                serializer.Invalidate();
                return false;
            }
        }
        if (!serializer.IsValid())
        {
            return false;
        }
        *this = AZStd::move(candidate);
        return true;
    }

    bool ScriptDefinedInputFrame::operator==(const ScriptDefinedInputFrame& rhs) const
    {
        return m_schemaFingerprint == rhs.m_schemaFingerprint && m_isTyped == rhs.m_isTyped && m_values == rhs.m_values &&
            m_typedValues == rhs.m_typedValues;
    }

    bool ScriptDefinedInputFrame::IsValid() const
    {
        if (m_typedValues.size() > ScriptDefinedInputCapacity)
        {
            return false;
        }
        for (const auto& value : m_typedValues)
        {
            if (!IsValidValue(value))
            {
                return false;
            }
        }
        for (float value : m_values)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        return true;
    }
} // namespace Multiplayer
