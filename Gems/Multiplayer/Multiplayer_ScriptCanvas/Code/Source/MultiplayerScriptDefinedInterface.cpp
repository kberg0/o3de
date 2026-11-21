/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "MultiplayerScriptDefinedInterface.h"
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Script/ScriptContextAttributes.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/string/conversions.h>
#include <Multiplayer/Components/NetBindComponent.h>
#include <Multiplayer/IMultiplayer.h>
#include <Multiplayer/NetworkEntity/INetworkEntityManager.h>
#include <AzCore/std/utility/charconv.h>
#include <cmath>
#include <limits>

namespace ScriptCanvasMultiplayer
{
    using namespace Multiplayer;

    namespace
    {
        //! Script Canvas category of the exact integer types.
        constexpr const char* ExactIntegerCategory = "Multiplayer/Exact integers";

        //! Name of the single parameter of generated setters and change notifications.
        constexpr const char* ValueParameterName = "Value";

        //! Converts a value to the type of its Script Canvas pin: the exact integer types for 64-bit integers, Number (double)
        //! for other numbers, AZStd::string for strings, and the local entity ID for a net entity (invalid when the entity is
        //! not present locally). Other types are their own pin type.
        template<class T>
        auto ToScriptPin(const T& item)
        {
            if constexpr (AZStd::is_same_v<T, int64_t>)
            {
                return ScriptDefinedInt64{ item, true };
            }
            else if constexpr (AZStd::is_same_v<T, uint64_t>)
            {
                return ScriptDefinedUInt64{ item, true };
            }
            else if constexpr ((AZStd::is_integral_v<T> && !AZStd::is_same_v<T, bool>) || AZStd::is_floating_point_v<T>)
            {
                return static_cast<double>(item);
            }
            else if constexpr (AZStd::is_same_v<T, ScriptDefinedString>)
            {
                return AZStd::string(item.data(), item.size());
            }
            else if constexpr (AZStd::is_same_v<T, NetEntityId>)
            {
                AZ::EntityId entityId;
                if (INetworkEntityManager* networkEntityManager = GetNetworkEntityManager())
                {
                    if (const auto entity = networkEntityManager->GetEntity(item); entity.Exists())
                    {
                        entityId = entity.GetEntity()->GetId();
                    }
                }
                return entityId;
            }
            else
            {
                return T(item);
            }
        }

        //! Derives a stable ID from a member ID and a salt, so regenerated interfaces keep their IDs.
        AZ::Uuid MemberId(const AZ::Uuid& id, AZStd::string_view salt)
        {
            AZStd::string key = id.ToFixedString().c_str();
            key.append(salt.data(), salt.size());
            return AZ::Uuid::CreateName(key.c_str());
        }

        //! Returns the argument converted to T, or nullptr if it cannot be converted.
        template<class T>
        T* ConvertArgument(AZ::BehaviorArgument& argument)
        {
            return argument.ConvertTo<T>() ? argument.GetAsUnsafe<T>() : nullptr;
        }

        //! Parses the whole text as a decimal integer. Trailing characters make the parse fail.
        template<class T>
        bool ParseDecimal(const AZStd::string& text, T& value)
        {
            const char* end = text.data() + text.size();
            const auto result = AZStd::from_chars(text.data(), end, value);
            return result.ec == AZStd::errc{} && result.ptr == end;
        }

        //! Returns a copy of the parameter named for use as the value of a setter or change notification.
        ScriptDefinedParameter MakeValueParameter(const ScriptDefinedParameter& field)
        {
            ScriptDefinedParameter value = field;
            value.m_name = ValueParameterName;
            return value;
        }

        //! Adds the getter, the optional setter and, for properties, the change notification of each field.
        //! @param methodInfix Distinguishes the field kind in method names, as in GetInput_Move.
        template<class Fields>
        void AddFieldMethods(
            AZStd::vector<ScriptDefinedMethod>& methods,
            const Fields& fields,
            ScriptDefinedOperation getOperation,
            ScriptDefinedOperation setOperation,
            const char* methodInfix,
            bool writable)
        {
            for (const auto& field : fields)
            {
                const AZStd::string suffix = AZStd::string(methodInfix) + "_" + field.m_name;
                methods.push_back({ getOperation, field.m_id, "Get" + suffix, {}, ScriptDefinedPinType(field.m_type) });

                if (writable)
                {
                    methods.push_back({ setOperation, field.m_id, "Set" + suffix, { MakeValueParameter(field) }, azrtti_typeid<bool>() });
                }

                if (getOperation == ScriptDefinedOperation::GetProperty)
                {
                    methods.push_back({ ScriptDefinedOperation::Notification,
                                        field.m_id,
                                        ScriptDefinedPropertyChangedPrefix + field.m_name,
                                        { MakeValueParameter(field) } });
                }
            }
        }
    } // namespace

    ScriptDefinedInt64 ScriptDefinedInt64::FromDecimal(const AZStd::string& text)
    {
        ScriptDefinedInt64 result;
        result.m_valid = ParseDecimal(text, result.m_value);
        return result;
    }

    AZStd::string ScriptDefinedInt64::ToDecimal() const
    {
        return m_valid ? AZStd::to_string(m_value) : AZStd::string{};
    }

    ScriptDefinedInt64 ScriptDefinedInt64::Add(ScriptDefinedInt64 other) const
    {
        const bool overflows = (other.m_value > 0 && m_value > std::numeric_limits<AZ::s64>::max() - other.m_value) ||
            (other.m_value < 0 && m_value < std::numeric_limits<AZ::s64>::min() - other.m_value);

        ScriptDefinedInt64 result;
        result.m_valid = m_valid && other.m_valid && !overflows;
        if (result.m_valid)
        {
            result.m_value = m_value + other.m_value;
        }
        return result;
    }

    ScriptDefinedUInt64 ScriptDefinedUInt64::FromDecimal(const AZStd::string& text)
    {
        ScriptDefinedUInt64 result;
        result.m_valid = ParseDecimal(text, result.m_value);
        return result;
    }

    AZStd::string ScriptDefinedUInt64::ToDecimal() const
    {
        return m_valid ? AZStd::to_string(m_value) : AZStd::string{};
    }

    ScriptDefinedUInt64 ScriptDefinedUInt64::Add(ScriptDefinedUInt64 other) const
    {
        const bool overflows = m_value > std::numeric_limits<AZ::u64>::max() - other.m_value;

        ScriptDefinedUInt64 result;
        result.m_valid = m_valid && other.m_valid && !overflows;
        if (result.m_valid)
        {
            result.m_value = m_value + other.m_value;
        }
        return result;
    }

    void ReflectScriptDefinedIntegers(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ScriptDefinedInt64>()
                ->Version(1)
                ->Field("Value", &ScriptDefinedInt64::m_value)
                ->Field("Valid", &ScriptDefinedInt64::m_valid);
            serializeContext->Class<ScriptDefinedUInt64>()
                ->Version(1)
                ->Field("Value", &ScriptDefinedUInt64::m_value)
                ->Field("Valid", &ScriptDefinedUInt64::m_valid);
        }

        if (auto* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->Class<ScriptDefinedInt64>("MultiplayerInt64")
                ->Attribute(AZ::Script::Attributes::EnableAsScriptEventParamType, true)
                ->Attribute(AZ::Script::Attributes::EnableAsScriptEventReturnType, true)
                ->Attribute(AZ::Script::Attributes::Category, ExactIntegerCategory)
                ->Attribute(AZ::Script::Attributes::Storage, AZ::Script::Attributes::StorageType::Value)
                ->Constructor<>()
                ->Method("FromDecimal", &ScriptDefinedInt64::FromDecimal)
                ->Method("ToDecimal", &ScriptDefinedInt64::ToDecimal)
                ->Method("IsValid", &ScriptDefinedInt64::IsValid)
                ->Method("Add", &ScriptDefinedInt64::Add)
                ->Method("Equal", &ScriptDefinedInt64::Equal)
                ->Method("Less", &ScriptDefinedInt64::Less);
            behaviorContext->Class<ScriptDefinedUInt64>("MultiplayerUInt64")
                ->Attribute(AZ::Script::Attributes::EnableAsScriptEventParamType, true)
                ->Attribute(AZ::Script::Attributes::EnableAsScriptEventReturnType, true)
                ->Attribute(AZ::Script::Attributes::Category, ExactIntegerCategory)
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
        return ToScriptPinValue(DefaultScriptDefinedValue(type)).type();
    }

    AZStd::vector<ScriptDefinedMethod> MakeScriptDefinedMethods(const MultiplayerScriptDefinedDefinition& definition)
    {
        AZStd::vector<ScriptDefinedMethod> methods;
        AddFieldMethods(
            methods, definition.m_properties, ScriptDefinedOperation::GetProperty, ScriptDefinedOperation::SetProperty, "", true);
        AddFieldMethods(methods, definition.m_inputs, ScriptDefinedOperation::GetInput, ScriptDefinedOperation::SetInput, "Input", true);
        AddFieldMethods(
            methods,
            definition.m_archetypes,
            ScriptDefinedOperation::GetArchetype,
            ScriptDefinedOperation::GetArchetype,
            "Archetype",
            false);

        for (const auto& rpc : definition.m_rpcs)
        {
            methods.push_back({ ScriptDefinedOperation::SendRpc, rpc.m_id, "Send_" + rpc.m_name, rpc.m_parameters, azrtti_typeid<bool>() });
            methods.push_back(
                { ScriptDefinedOperation::Notification, rpc.m_id, ScriptDefinedRpcReceivedPrefix + rpc.m_name, rpc.m_parameters });
        }

        // Input notifications exist only when the schema declares inputs.
        if (!definition.m_inputs.empty())
        {
            ScriptDefinedParameter deltaTime;
            deltaTime.m_id = MemberId(definition.m_id, "DeltaTime");
            deltaTime.m_name = "DeltaTime";
            deltaTime.m_type = ScriptDefinedRpcType::Float;

            ScriptDefinedParameter reprocessing;
            reprocessing.m_id = MemberId(definition.m_id, "Reprocessing");
            reprocessing.m_name = "IsReprocessing";
            reprocessing.m_type = ScriptDefinedRpcType::Boolean;

            methods.push_back({ ScriptDefinedOperation::Notification,
                                MemberId(definition.m_id, "Create"),
                                ScriptDefinedCreateInputEvent,
                                { deltaTime } });
            methods.push_back({ ScriptDefinedOperation::Notification,
                                MemberId(definition.m_id, "Process"),
                                ScriptDefinedProcessInputEvent,
                                { deltaTime, reprocessing } });
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
            // Salt the member ID with the method kind (the name up to its first '_'), so that the getter,
            // setter and notification of one member get distinct IDs.
            const AZStd::string methodKind = entry.m_name.substr(0, entry.m_name.find('_'));
            const AZ::Uuid methodId = MemberId(entry.m_memberId, methodKind);

            auto& method = result.NewMethod();
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

                const AZ::Uuid parameterId = parameter.GetNameProperty().GetId();
                parameter.GetTypeProperty().Set(ScriptDefinedPinType(field.m_type));
                parameter.GetTypeProperty().SetId(MemberId(parameterId, "Type"));
                parameter.GetTooltipProperty().SetId(MemberId(parameterId, "Tooltip"));
            }
        }

        result.SetVersion(definition.m_revision);
        result.Flatten();
        return result;
    }

    bool ReadScriptDefinedArgument(ScriptDefinedRpcType type, AZ::BehaviorArgument& argument, ScriptDefinedRpcValue& value)
    {
        value = DefaultScriptDefinedValue(type);
        bool converted = false;
        AZStd::visit(
            [&argument, &converted](auto& item)
            {
                using T = AZStd::decay_t<decltype(item)>;
                if constexpr (AZStd::is_same_v<T, int64_t>)
                {
                    const ScriptDefinedInt64* exact = ConvertArgument<ScriptDefinedInt64>(argument);
                    if (exact && exact->m_valid)
                    {
                        item = exact->m_value;
                        converted = true;
                    }
                }
                else if constexpr (AZStd::is_same_v<T, uint64_t>)
                {
                    const ScriptDefinedUInt64* exact = ConvertArgument<ScriptDefinedUInt64>(argument);
                    if (exact && exact->m_valid)
                    {
                        item = exact->m_value;
                        converted = true;
                    }
                }
                else if constexpr ((AZStd::is_integral_v<T> && !AZStd::is_same_v<T, bool>) || AZStd::is_floating_point_v<T>)
                {
                    // Narrower numbers arrive as Number (double); reject values the target type cannot hold.
                    const double* number = ConvertArgument<double>(argument);
                    const bool inRange = number && std::isfinite(*number) &&
                        *number >= static_cast<double>(std::numeric_limits<T>::lowest()) &&
                        *number <= static_cast<double>(std::numeric_limits<T>::max());
                    if (!inRange)
                    {
                        return;
                    }

                    if constexpr (AZStd::is_integral_v<T>)
                    {
                        if (std::trunc(*number) != *number)
                        {
                            return;
                        }
                    }

                    item = static_cast<T>(*number);
                    converted = true;
                }
                else if constexpr (AZStd::is_same_v<T, ScriptDefinedString>)
                {
                    const AZStd::string* text = ConvertArgument<AZStd::string>(argument);
                    if (text && text->size() <= item.max_size())
                    {
                        item.assign(text->data(), text->size());
                        converted = true;
                    }
                }
                else if constexpr (AZStd::is_same_v<T, NetEntityId>)
                {
                    const AZ::EntityId* entityId = ConvertArgument<AZ::EntityId>(argument);
                    if (!entityId)
                    {
                        return;
                    }

                    // An invalid entity is sent as an invalid net entity; a valid one must be networked.
                    if (!entityId->IsValid())
                    {
                        item = InvalidNetEntityId;
                        converted = true;
                        return;
                    }

                    AZ::Entity* entity = nullptr;
                    AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, *entityId);
                    if (auto* netBindComponent = entity ? entity->FindComponent<NetBindComponent>() : nullptr)
                    {
                        item = netBindComponent->GetNetEntityId();
                        converted = true;
                    }
                }
                else if (const T* direct = ConvertArgument<T>(argument))
                {
                    item = *direct;
                    converted = true;
                }
            },
            value);

        return converted && IsFiniteScriptDefinedValue(value);
    }

    AZStd::any ToScriptPinValue(const ScriptDefinedRpcValue& value)
    {
        return AZStd::visit(
            [](const auto& item)
            {
                return AZStd::any(ToScriptPin(item));
            },
            value);
    }

    bool StoreScriptDefinedResult(const ScriptDefinedRpcValue& value, AZ::BehaviorArgument& result)
    {
        return AZStd::visit(
            [&result](const auto& item)
            {
                return result.StoreResult(ToScriptPin(item));
            },
            value);
    }
} // namespace ScriptCanvasMultiplayer
