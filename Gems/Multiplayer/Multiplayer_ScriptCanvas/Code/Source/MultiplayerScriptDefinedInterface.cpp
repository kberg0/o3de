/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "MultiplayerScriptDefinedInterface.h"
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Script/ScriptContextAttributes.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <Multiplayer/Components/NetBindComponent.h>
#include <Multiplayer/IMultiplayer.h>
#include <Multiplayer/NetworkEntity/INetworkEntityManager.h>
#include <charconv>
#include <cmath>
#include <limits>

namespace ScriptCanvasMultiplayer
{
    using namespace Multiplayer;
    namespace
    {
        AZ::Uuid MemberId(const AZ::Uuid& id, AZStd::string_view suffix)
        {
            AZStd::string key = id.ToFixedString().c_str();
            key.append(suffix.data(), suffix.size());
            return AZ::Uuid::CreateName(key.c_str());
        }
        template<class T>
        T* Argument(AZ::BehaviorArgument& argument)
        {
            return argument.ConvertTo<T>() ? argument.GetAsUnsafe<T>() : nullptr;
        }
    } // namespace

    ScriptDefinedInt64 ScriptDefinedInt64::FromDecimal(const AZStd::string& text)
    {
        ScriptDefinedInt64 result;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result.m_value);
        result.m_valid = parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
        return result;
    }
    AZStd::string ScriptDefinedInt64::ToDecimal() const
    {
        return m_valid ? AZStd::string::format("%lld", static_cast<long long>(m_value)) : AZStd::string{};
    }
    ScriptDefinedInt64 ScriptDefinedInt64::Add(ScriptDefinedInt64 other) const
    {
        ScriptDefinedInt64 result;
        result.m_valid = m_valid && other.m_valid &&
            !((other.m_value > 0 && m_value > std::numeric_limits<AZ::s64>::max() - other.m_value) ||
              (other.m_value < 0 && m_value < std::numeric_limits<AZ::s64>::min() - other.m_value));
        if (result.m_valid)
        {
            result.m_value = m_value + other.m_value;
        }
        return result;
    }

    ScriptDefinedUInt64 ScriptDefinedUInt64::FromDecimal(const AZStd::string& text)
    {
        ScriptDefinedUInt64 result;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result.m_value);
        result.m_valid = parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
        return result;
    }
    AZStd::string ScriptDefinedUInt64::ToDecimal() const
    {
        return m_valid ? AZStd::string::format("%llu", static_cast<unsigned long long>(m_value)) : AZStd::string{};
    }
    ScriptDefinedUInt64 ScriptDefinedUInt64::Add(ScriptDefinedUInt64 other) const
    {
        ScriptDefinedUInt64 result;
        result.m_valid = m_valid && other.m_valid && m_value <= std::numeric_limits<AZ::u64>::max() - other.m_value;
        if (result.m_valid)
        {
            result.m_value = m_value + other.m_value;
        }
        return result;
    }
    void ReflectScriptDefinedIntegers(AZ::ReflectContext* context)
    {
        if (auto* sc = azrtti_cast<AZ::SerializeContext*>(context))
        {
            sc->Class<ScriptDefinedInt64>()
                ->Version(1)
                ->Field("Value", &ScriptDefinedInt64::m_value)
                ->Field("Valid", &ScriptDefinedInt64::m_valid);
            sc->Class<ScriptDefinedUInt64>()
                ->Version(1)
                ->Field("Value", &ScriptDefinedUInt64::m_value)
                ->Field("Valid", &ScriptDefinedUInt64::m_valid);
        }
        if (auto* bc = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            bc->Class<ScriptDefinedInt64>("MultiplayerInt64")
                ->Attribute(AZ::Script::Attributes::EnableAsScriptEventParamType, true)
                ->Attribute(AZ::Script::Attributes::EnableAsScriptEventReturnType, true)
                ->Attribute(AZ::Script::Attributes::Category, "Multiplayer/Exact integers")
                ->Attribute(AZ::Script::Attributes::Storage, AZ::Script::Attributes::StorageType::Value)
                ->Constructor<>()
                ->Method("FromDecimal", &ScriptDefinedInt64::FromDecimal)
                ->Method("ToDecimal", &ScriptDefinedInt64::ToDecimal)
                ->Method("IsValid", &ScriptDefinedInt64::IsValid)
                ->Method("Add", &ScriptDefinedInt64::Add)
                ->Method("Equal", &ScriptDefinedInt64::Equal)
                ->Method("Less", &ScriptDefinedInt64::Less);
            bc->Class<ScriptDefinedUInt64>("MultiplayerUInt64")
                ->Attribute(AZ::Script::Attributes::EnableAsScriptEventParamType, true)
                ->Attribute(AZ::Script::Attributes::EnableAsScriptEventReturnType, true)
                ->Attribute(AZ::Script::Attributes::Category, "Multiplayer/Exact integers")
                ->Attribute(AZ::Script::Attributes::Storage, AZ::Script::Attributes::StorageType::Value)
                ->Constructor<>()
                ->Method("FromDecimal", &ScriptDefinedUInt64::FromDecimal)
                ->Method("ToDecimal", &ScriptDefinedUInt64::ToDecimal)
                ->Method("IsValid", &ScriptDefinedUInt64::IsValid)
                ->Method("Add", &ScriptDefinedUInt64::Add)
                ->Method("Equal", &ScriptDefinedUInt64::Equal)
                ->Method("Less", &ScriptDefinedUInt64::Less);
        }
    }
    AZ::Uuid ScriptDefinedPinType(ScriptDefinedRpcType type)
    {
        switch (type)
        {
        case ScriptDefinedRpcType::Boolean:
            return azrtti_typeid<bool>();
        case ScriptDefinedRpcType::Int64:
            return azrtti_typeid<ScriptDefinedInt64>();
        case ScriptDefinedRpcType::UInt64:
            return azrtti_typeid<ScriptDefinedUInt64>();
        case ScriptDefinedRpcType::Vector2:
            return azrtti_typeid<AZ::Vector2>();
        case ScriptDefinedRpcType::Vector3:
            return azrtti_typeid<AZ::Vector3>();
        case ScriptDefinedRpcType::Quaternion:
            return azrtti_typeid<AZ::Quaternion>();
        case ScriptDefinedRpcType::String:
            return azrtti_typeid<AZStd::string>();
        case ScriptDefinedRpcType::Entity:
            return azrtti_typeid<AZ::EntityId>();
        default:
            return azrtti_typeid<double>();
        }
    }
    AZStd::vector<ScriptDefinedMethod> MakeScriptDefinedMethods(const MultiplayerScriptDefinedDefinition& definition)
    {
        AZStd::vector<ScriptDefinedMethod> methods;
        auto fieldMethods =
            [&](const auto& fields, ScriptDefinedOperation get, ScriptDefinedOperation set, const char* prefix, bool writable)
        {
            for (const auto& field : fields)
            {
                methods.push_back(
                    { get, field.m_id, AZStd::string("Get") + prefix + "_" + field.m_name, {}, ScriptDefinedPinType(field.m_type) });
                if (writable)
                {
                    ScriptDefinedParameter value = field;
                    value.m_name = "Value";
                    methods.push_back(
                        { set, field.m_id, AZStd::string("Set") + prefix + "_" + field.m_name, { value }, azrtti_typeid<bool>() });
                }
                if (get == ScriptDefinedOperation::GetProperty)
                {
                    ScriptDefinedParameter value = field;
                    value.m_name = "Value";
                    methods.push_back({ ScriptDefinedOperation::Notification, field.m_id, "On_" + field.m_name, { value } });
                }
            }
        };
        fieldMethods(definition.m_properties, ScriptDefinedOperation::GetProperty, ScriptDefinedOperation::SetProperty, "", true);
        fieldMethods(definition.m_inputs, ScriptDefinedOperation::GetInput, ScriptDefinedOperation::SetInput, "Input", true);
        fieldMethods(
            definition.m_archetypes, ScriptDefinedOperation::GetArchetype, ScriptDefinedOperation::GetArchetype, "Archetype", false);
        for (const auto& rpc : definition.m_rpcs)
        {
            methods.push_back({ ScriptDefinedOperation::SendRpc, rpc.m_id, "Send_" + rpc.m_name, rpc.m_parameters, azrtti_typeid<bool>() });
            methods.push_back({ ScriptDefinedOperation::Notification, rpc.m_id, "OnRpc_" + rpc.m_name, rpc.m_parameters });
        }
        if (!definition.m_inputs.empty())
        {
            ScriptDefinedParameter delta;
            delta.m_id = MemberId(definition.m_id, "DeltaTime");
            delta.m_name = "DeltaTime";
            delta.m_type = ScriptDefinedRpcType::Float;
            ScriptDefinedParameter replay;
            replay.m_id = MemberId(definition.m_id, "Reprocessing");
            replay.m_name = "IsReprocessing";
            replay.m_type = ScriptDefinedRpcType::Boolean;
            methods.push_back({ ScriptDefinedOperation::Notification, MemberId(definition.m_id, "Create"), "OnCreateInput", { delta } });
            methods.push_back(
                { ScriptDefinedOperation::Notification, MemberId(definition.m_id, "Process"), "OnProcessInput", { delta, replay } });
        }
        return methods;
    }
    ScriptEvents::ScriptEvent MakeScriptDefinedInterface(const MultiplayerScriptDefinedDefinition& definition)
    {
        ScriptEvents::ScriptEvent result;
        result.SetGenerated(true);
        // Include the schema UUID to avoid collisions between assets with the same display name.
        result.GetNameProperty().Set(definition.m_name + "_" + definition.m_id.ToFixedString(false, false).c_str());
        result.GetNameProperty().SetId(definition.m_id);
        result.GetCategoryProperty().Set(AZStd::string("Multiplayer/") + definition.m_name);
        result.GetCategoryProperty().SetId(MemberId(definition.m_id, "Category"));
        result.GetTooltipProperty().Set(AZStd::string("Schema-defined multiplayer interface. Address is the entity ID."));
        result.GetTooltipProperty().SetId(MemberId(definition.m_id, "Tooltip"));
        result.GetAddressTypeProperty().Set(azrtti_typeid<AZ::EntityId>());
        result.GetAddressTypeProperty().SetId(MemberId(definition.m_id, "Address"));
        for (const auto& entry : MakeScriptDefinedMethods(definition))
        {
            auto& method = result.NewMethod();
            const auto methodId = MemberId(entry.m_memberId, entry.m_name.substr(0, entry.m_name.find('_')));
            method.GetNameProperty().Set(AZStd::string(entry.m_name));
            method.GetNameProperty().SetId(methodId);
            method.GetReturnTypeProperty().Set(AZ::Uuid(entry.m_returnType));
            method.GetReturnTypeProperty().SetId(MemberId(methodId, "Result"));
            method.GetTooltipProperty().SetId(MemberId(methodId, "Tooltip"));
            for (const auto& field : entry.m_parameters)
            {
                auto& parameter = method.NewParameter();
                parameter.GetNameProperty().Set(AZStd::string(field.m_name));
                parameter.GetNameProperty().SetId(MemberId(methodId, field.m_id.ToFixedString().c_str()));
                parameter.GetTypeProperty().Set(ScriptDefinedPinType(field.m_type));
                parameter.GetTypeProperty().SetId(MemberId(parameter.GetNameProperty().GetId(), "Type"));
                parameter.GetTooltipProperty().SetId(MemberId(parameter.GetNameProperty().GetId(), "Tooltip"));
            }
        }
        result.SetVersion(definition.m_revision);
        result.Flatten();
        return result;
    }

    bool ReadScriptDefinedArgument(ScriptDefinedRpcType type, AZ::BehaviorArgument& argument, ScriptDefinedRpcValue& value)
    {
        value = DefaultScriptDefinedValue(type);
        bool valid = false;
        AZStd::visit(
            [&](auto& item)
            {
                using T = AZStd::decay_t<decltype(item)>;
                if constexpr (AZStd::is_same_v<T, int64_t>)
                {
                    if (auto* p = Argument<ScriptDefinedInt64>(argument); p && p->m_valid)
                    {
                        item = p->m_value;
                        valid = true;
                    }
                }
                else if constexpr (AZStd::is_same_v<T, uint64_t>)
                {
                    if (auto* p = Argument<ScriptDefinedUInt64>(argument); p && p->m_valid)
                    {
                        item = p->m_value;
                        valid = true;
                    }
                }
                else if constexpr ((AZStd::is_integral_v<T> && !AZStd::is_same_v<T, bool>) || AZStd::is_floating_point_v<T>)
                {
                    if (auto* p = Argument<double>(argument); p && std::isfinite(*p) &&
                        *p >= static_cast<double>(std::numeric_limits<T>::lowest()) &&
                        *p <= static_cast<double>(std::numeric_limits<T>::max()))
                    {
                        if constexpr (AZStd::is_integral_v<T>)
                        {
                            if (std::trunc(*p) != *p)
                            {
                                return;
                            }
                        }
                        item = static_cast<T>(*p);
                        valid = true;
                    }
                }
                else if constexpr (AZStd::is_same_v<T, ScriptDefinedString>)
                {
                    if (auto* p = Argument<AZStd::string>(argument); p && p->size() <= item.max_size())
                    {
                        item.assign(p->data(), p->size());
                        valid = true;
                    }
                }
                else if constexpr (AZStd::is_same_v<T, NetEntityId>)
                {
                    if (auto* p = Argument<AZ::EntityId>(argument))
                    {
                        if (!p->IsValid())
                        {
                            item = InvalidNetEntityId;
                            valid = true;
                        }
                        else
                        {
                            AZ::Entity* entity = nullptr;
                            AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, *p);
                            if (auto* binding = entity ? entity->FindComponent<NetBindComponent>() : nullptr)
                            {
                                item = binding->GetNetEntityId();
                                valid = true;
                            }
                        }
                    }
                }
                else if (auto* p = Argument<T>(argument))
                {
                    item = *p;
                    valid = true;
                }
            },
            value);
        ScriptDefinedRpcPayload check;
        check.m_arguments.push_back(value);
        return valid && check.IsValid();
    }
    bool StoreScriptDefinedResult(const ScriptDefinedRpcValue& value, AZ::BehaviorArgument& result)
    {
        return AZStd::visit(
            [&](const auto& item) -> bool
            {
                using T = AZStd::decay_t<decltype(item)>;
                if constexpr (AZStd::is_same_v<T, int64_t>)
                {
                    return result.StoreResult(ScriptDefinedInt64{ item, true });
                }
                else if constexpr (AZStd::is_same_v<T, uint64_t>)
                {
                    return result.StoreResult(ScriptDefinedUInt64{ item, true });
                }
                else if constexpr ((AZStd::is_integral_v<T> && !AZStd::is_same_v<T, bool>) || AZStd::is_floating_point_v<T>)
                {
                    return result.StoreResult(static_cast<double>(item));
                }
                else if constexpr (AZStd::is_same_v<T, ScriptDefinedString>)
                {
                    return result.StoreResult(AZStd::string(item.data(), item.size()));
                }
                else if constexpr (AZStd::is_same_v<T, NetEntityId>)
                {
                    AZ::EntityId entityId;
                    if (auto* manager = GetNetworkEntityManager())
                    {
                        if (const auto entity = manager->GetEntity(item); entity.Exists())
                        {
                            entityId = entity.GetEntity()->GetId();
                        }
                    }
                    return result.StoreResult(AZ::EntityId(entityId));
                }
                else
                {
                    return result.StoreResult(T(item));
                }
            },
            value);
    }
} // namespace ScriptCanvasMultiplayer
