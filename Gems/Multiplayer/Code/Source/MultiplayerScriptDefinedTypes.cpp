/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/typetraits/void_t.h>
#include <AzCore/std/utility/declval.h>
#include <Multiplayer/MultiplayerScriptDefinedDefinition.h>
#include <Multiplayer/MultiplayerScriptDefinedTypes.h>
#include <cmath>

namespace Multiplayer
{
    namespace
    {
        //! True for the math types, which check all of their components with IsFinite().
        template<class T, class = void>
        struct HasIsFinite : AZStd::false_type
        {
        };

        template<class T>
        struct HasIsFinite<T, AZStd::void_t<decltype(AZStd::declval<const T&>().IsFinite())>> : AZStd::true_type
        {
        };

        //! Serializes the type tag and then the value. Does not invalidate the serializer on failure.
        bool SerializeTaggedValue(AzNetworking::ISerializer& serializer, ScriptDefinedRpcValue& value)
        {
            uint8_t typeTag = aznumeric_cast<uint8_t>(value.index());
            if (!serializer.Serialize(typeTag, "Type"))
            {
                return false;
            }

            // When reading a different type, switch the value to the alternative named by the tag before reading into it.
            // Reading into the current alternative instead lets change tracking compare against the previous value.
            const bool isReading = serializer.GetSerializerMode() == AzNetworking::SerializerMode::WriteToObject;
            if (isReading && typeTag != value.index())
            {
                if (typeTag >= ScriptDefinedTypeCount)
                {
                    return false;
                }
                value = DefaultScriptDefinedValue(static_cast<ScriptDefinedRpcType>(typeTag));
            }

            const bool serialized = AZStd::visit(
                [&serializer](auto& element)
                {
                    return serializer.Serialize(element, "Value");
                },
                value);

            return serialized && IsFiniteScriptDefinedValue(value);
        }
    } // namespace

    bool IsFiniteScriptDefinedValue(const ScriptDefinedRpcValue& value)
    {
        return AZStd::visit(
            [](const auto& element)
            {
                using Type = AZStd::decay_t<decltype(element)>;
                if constexpr (AZStd::is_floating_point_v<Type>)
                {
                    return std::isfinite(element);
                }
                else if constexpr (HasIsFinite<Type>::value)
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

    bool SerializeScriptDefinedValue(AzNetworking::ISerializer& serializer, ScriptDefinedRpcValue& value)
    {
        if (!SerializeTaggedValue(serializer, value))
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
        // Read into a copy so that a rejected payload leaves this one unchanged.
        ScriptDefinedRpcPayload candidate = *this;
        uint8_t argumentCount = aznumeric_cast<uint8_t>(candidate.m_arguments.size());

        const bool headerSerialized = serializer.Serialize(candidate.m_schemaFingerprint, "Schema") &&
            serializer.Serialize(candidate.m_rpcId, "Rpc") && serializer.Serialize(argumentCount, "Count");
        if (!headerSerialized || argumentCount > ScriptDefinedArgumentCapacity)
        {
            serializer.Invalidate();
            return false;
        }

        candidate.m_arguments.resize(argumentCount);
        for (uint32_t index = 0; index < argumentCount; ++index)
        {
            const auto label = AzNetworking::GenerateIndexLabel<ScriptDefinedArgumentCapacity>(index);
            const bool argumentSerialized = serializer.BeginObject(label.c_str()) &&
                SerializeTaggedValue(serializer, candidate.m_arguments[index]) && serializer.EndObject(label.c_str());
            if (!argumentSerialized)
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
        return AZStd::all_of(m_arguments.begin(), m_arguments.end(), &IsFiniteScriptDefinedValue);
    }

    bool ScriptDefinedInputFrame::Serialize(AzNetworking::ISerializer& serializer)
    {
        if (m_typedValues.size() > ScriptDefinedInputCapacity)
        {
            serializer.Invalidate();
            return false;
        }

        // Read into a copy so that a rejected frame leaves this one unchanged.
        ScriptDefinedInputFrame candidate = *this;
        if (!serializer.Serialize(candidate.m_isTyped, "Typed"))
        {
            serializer.Invalidate();
            return false;
        }

        const size_t currentCount = candidate.m_isTyped ? candidate.m_typedValues.size() : candidate.m_values.size();
        uint8_t valueCount = aznumeric_cast<uint8_t>(currentCount);
        const bool headerSerialized =
            serializer.Serialize(candidate.m_schemaFingerprint, "Schema") && serializer.Serialize(valueCount, "Count");
        if (!headerSerialized || valueCount > ScriptDefinedInputCapacity)
        {
            serializer.Invalidate();
            return false;
        }

        // A frame holds either typed or untyped values, never both.
        if (candidate.m_isTyped)
        {
            candidate.m_typedValues.resize(valueCount);
            candidate.m_values.clear();
        }
        else
        {
            candidate.m_values.resize(valueCount);
            candidate.m_typedValues.clear();
        }

        for (uint32_t index = 0; index < valueCount; ++index)
        {
            const auto label = AzNetworking::GenerateIndexLabel<ScriptDefinedInputCapacity>(index);
            bool valueSerialized = false;
            if (candidate.m_isTyped)
            {
                valueSerialized = serializer.BeginObject(label.c_str()) &&
                    SerializeScriptDefinedValue(serializer, candidate.m_typedValues[index]) && serializer.EndObject(label.c_str());
            }
            else
            {
                valueSerialized =
                    serializer.Serialize(candidate.m_values[index], label.c_str()) && std::isfinite(candidate.m_values[index]);
            }

            if (!valueSerialized)
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

        const bool typedValuesFinite = AZStd::all_of(m_typedValues.begin(), m_typedValues.end(), &IsFiniteScriptDefinedValue);
        const bool valuesFinite = AZStd::all_of(
            m_values.begin(),
            m_values.end(),
            [](float value)
            {
                return std::isfinite(value);
            });
        return typedValuesFinite && valuesFinite;
    }
} // namespace Multiplayer
