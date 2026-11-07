/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "MultiplayerScriptDefinedBridge.h"
#include <AzCore/Serialization/SerializeContext.h>
#include <ScriptEvents/ScriptEventsBus.h>

namespace ScriptCanvasMultiplayer
{
    using namespace Multiplayer;
    void MultiplayerScriptDefinedBridge::Reflect(AZ::ReflectContext* context)
    {
        MultiplayerScriptDefinedDefinition::Reflect(context);
        if (auto* sc = azrtti_cast<AZ::SerializeContext*>(context))
        {
            sc->Class<MultiplayerScriptDefinedBridge, AZ::Component>()->Version(1)->Field(
                "Interface", &MultiplayerScriptDefinedBridge::m_interfaceAsset);
        }
    }
    void MultiplayerScriptDefinedBridge::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        services.push_back(AZ_CRC_CE("MultiplayerScriptDefinedComponent"));
    }
    void MultiplayerScriptDefinedBridge::Configure(AZ::Data::Asset<ScriptEvents::ScriptEventsAsset> interfaceAsset)
    {
        m_interfaceAsset = AZStd::move(interfaceAsset);
    }
    void MultiplayerScriptDefinedBridge::Init()
    {
        Bind();
    }
    void MultiplayerScriptDefinedBridge::Activate()
    {
        if (!Bind())
        {
            AZ_Error("Multiplayer", false, "Cannot bind Script Defined interface for %s", m_interfaceAsset.GetHint().c_str());
            return;
        }
    }
    void MultiplayerScriptDefinedBridge::Deactivate()
    {
        ScriptDefinedNotificationBus::Handler::BusDisconnect();
        Unbind();
    }
    MultiplayerScriptDefinedBridge::~MultiplayerScriptDefinedBridge()
    {
        Unbind();
    }
    bool MultiplayerScriptDefinedBridge::Bind()
    {
        if (m_handler)
        {
            return true;
        }
        ScriptDefinedRequestBus::EventResult(m_definition, GetEntityId(), &ScriptDefinedRequests::GetDefinition);
        if (!m_interfaceAsset.IsReady() || !m_definition || !m_definition->Validate())
        {
            return false;
        }
        const auto expected = MakeScriptDefinedInterface(*m_definition);
        const auto& actual = m_interfaceAsset->m_definition;
        if (actual.GetName() != expected.GetName() || actual.GetVersion() != expected.GetVersion())
        {
            return false;
        }
        ScriptEvents::ScriptEventBus::BroadcastResult(
            m_registration, &ScriptEvents::ScriptEventRequests::RegisterScriptEvent, m_interfaceAsset.GetId(), actual.GetVersion());
        if (!m_registration)
        {
            return false;
        }
        m_registration->CompleteRegistration(m_interfaceAsset);
        m_bus = m_registration->GetBehaviorBus(actual.GetVersion());
        if (!m_bus || !m_bus->m_createHandler || !m_bus->m_createHandler->InvokeResult(m_handler) || !m_handler)
        {
            Unbind();
            return false;
        }
        m_methods = MakeScriptDefinedMethods(*m_definition);
        for (const auto& method : m_methods)
        {
            if (method.m_operation != ScriptDefinedOperation::Notification &&
                !m_handler->InstallGenericHook(method.m_name.c_str(), &Hook, this))
            {
                Unbind();
                return false;
            }
        }
        AZ::EntityId entityId = GetEntityId();
        AZ::BehaviorArgument address;
        address.Set(&entityId);
        if (!m_handler->Connect(&address))
        {
            Unbind();
            return false;
        }
        ScriptDefinedNotificationBus::Handler::BusConnect(GetEntityId());
        return true;
    }
    void MultiplayerScriptDefinedBridge::Unbind()
    {
        ScriptDefinedNotificationBus::Handler::BusDisconnect();
        if (m_handler)
        {
            m_handler->Disconnect();
            m_bus->m_destroyHandler->Invoke(m_handler);
            m_handler = nullptr;
        }
        m_bus = nullptr;
        m_registration.reset();
        m_methods.clear();
        m_definition = nullptr;
    }
    void MultiplayerScriptDefinedBridge::Hook(
        void* instance, const char* name, int, AZ::BehaviorArgument* result, int count, AZ::BehaviorArgument* arguments)
    {
        static_cast<MultiplayerScriptDefinedBridge*>(instance)->Dispatch(name, result, count, arguments);
    }
    void MultiplayerScriptDefinedBridge::Dispatch(
        const char* name, AZ::BehaviorArgument* result, int count, AZ::BehaviorArgument* arguments)
    {
        const ScriptDefinedMethod* method = nullptr;
        for (const auto& entry : m_methods)
        {
            if (entry.m_name == name)
            {
                method = &entry;
                break;
            }
        }
        if (!method || count < 0 || static_cast<size_t>(count) != method->m_parameters.size())
        {
            return;
        }
        AZStd::fixed_vector<ScriptDefinedRpcValue, ScriptDefinedArgumentCapacity> values;
        bool valid = true;
        for (int i = 0; i < count; ++i)
        {
            ScriptDefinedRpcValue value;
            if (!ReadScriptDefinedArgument(method->m_parameters[i].m_type, arguments[i], value))
            {
                valid = false;
                break;
            }
            values.push_back(AZStd::move(value));
        }
        bool success = false;
        if (valid)
        {
            switch (method->m_operation)
            {
            case ScriptDefinedOperation::SetProperty:
                ScriptDefinedRequestBus::EventResult(
                    success, GetEntityId(), &ScriptDefinedRequests::WriteProperty, method->m_memberId, values[0]);
                break;
            case ScriptDefinedOperation::SetInput:
                ScriptDefinedInputRequestBus::EventResult(
                    success, GetEntityId(), &ScriptDefinedInputRequests::WriteInput, method->m_memberId, values[0]);
                break;
            case ScriptDefinedOperation::SendRpc:
                ScriptDefinedRequestBus::EventResult(success, GetEntityId(), &ScriptDefinedRequests::InvokeRpc, method->m_memberId, values);
                break;
            default:
                {
                    const auto* field = method->m_operation == ScriptDefinedOperation::GetProperty
                        ? m_definition->FindProperty(method->m_memberId)
                        : method->m_operation == ScriptDefinedOperation::GetArchetype ? m_definition->FindArchetype(method->m_memberId)
                                                                                      : nullptr;
                    if (!field)
                    {
                        for (const auto& input : m_definition->m_inputs)
                        {
                            if (input.m_id == method->m_memberId)
                            {
                                field = &input;
                                break;
                            }
                        }
                    }
                    if (!field)
                    {
                        return;
                    }
                    auto value = DefaultScriptDefinedValue(field->m_type);
                    if (method->m_operation == ScriptDefinedOperation::GetProperty)
                    {
                        ScriptDefinedRequestBus::EventResult(
                            success, GetEntityId(), &ScriptDefinedRequests::ReadProperty, field->m_id, value);
                    }
                    else if (method->m_operation == ScriptDefinedOperation::GetInput)
                    {
                        ScriptDefinedInputRequestBus::EventResult(
                            success, GetEntityId(), &ScriptDefinedInputRequests::ReadInput, field->m_id, value);
                    }
                    else
                    {
                        ScriptDefinedRequestBus::EventResult(
                            success, GetEntityId(), &ScriptDefinedRequests::ReadArchetype, field->m_id, value);
                    }
                    if (result)
                    {
                        StoreScriptDefinedResult(value, *result);
                    }
                    return;
                }
            }
        }
        if (result)
        {
            result->StoreResult(bool(success));
        }
    }

    void MultiplayerScriptDefinedBridge::Notify(
        const AZStd::string& name, const AZStd::fixed_vector<ScriptDefinedRpcValue, ScriptDefinedArgumentCapacity>& values)
    {
        if (!m_bus)
        {
            return;
        }
        auto found = m_bus->m_events.find(name);
        if (found == m_bus->m_events.end() || !found->second.m_event)
        {
            return;
        }
        using ScriptValue = AZStd::variant<
            bool,
            double,
            AZ::Vector2,
            AZ::Vector3,
            AZ::Quaternion,
            AZStd::string,
            AZ::EntityId,
            ScriptDefinedInt64,
            ScriptDefinedUInt64>;
        AZStd::fixed_vector<ScriptValue, ScriptDefinedArgumentCapacity> storage;
        AZStd::array<AZ::BehaviorArgument, ScriptDefinedArgumentCapacity + 1> arguments;
        AZ::EntityId entityId = GetEntityId();
        arguments[0].Set(&entityId);
        storage.resize(values.size());
        for (size_t i = 0; i < values.size(); ++i)
        {
            AZStd::visit(
                [&](const auto& item)
                {
                    using T = AZStd::decay_t<decltype(item)>;
                    if constexpr (AZStd::is_same_v<T, int64_t>)
                    {
                        storage[i] = ScriptDefinedInt64{};
                    }
                    else if constexpr (AZStd::is_same_v<T, uint64_t>)
                    {
                        storage[i] = ScriptDefinedUInt64{};
                    }
                    else if constexpr (AZStd::is_same_v<T, ScriptDefinedString>)
                    {
                        storage[i] = AZStd::string{};
                    }
                    else if constexpr (AZStd::is_same_v<T, NetEntityId>)
                    {
                        storage[i] = AZ::EntityId{};
                    }
                    else if constexpr ((AZStd::is_integral_v<T> && !AZStd::is_same_v<T, bool>) || AZStd::is_floating_point_v<T>)
                    {
                        storage[i] = double{};
                    }
                    else
                    {
                        storage[i] = T{};
                    }
                },
                values[i]);
            AZStd::visit(
                [&](auto& item)
                {
                    arguments[i + 1].Set(&item);
                },
                storage[i]);
            if (!StoreScriptDefinedResult(values[i], arguments[i + 1]))
            {
                return;
            }
        }
        found->second.m_event->Call(arguments.data(), aznumeric_cast<unsigned>(values.size() + 1));
    }
    void MultiplayerScriptDefinedBridge::OnPropertyChanged(const AZ::Uuid& id, const ScriptDefinedRpcValue& value)
    {
        if (const auto* field = m_definition->FindProperty(id))
        {
            Notify("On_" + field->m_name, { value });
        }
    }
    void MultiplayerScriptDefinedBridge::OnRpcReceived(uint16_t id, const ScriptDefinedRpcPayload& payload)
    {
        for (const auto& rpc : m_definition->m_rpcs)
        {
            if (m_definition->GetRpcId(rpc.m_id) == id)
            {
                Notify("OnRpc_" + rpc.m_name, payload.m_arguments);
                return;
            }
        }
    }
    void MultiplayerScriptDefinedBridge::OnCreateInput(float deltaTime)
    {
        Notify("OnCreateInput", { ScriptDefinedRpcValue(deltaTime) });
    }
    void MultiplayerScriptDefinedBridge::OnProcessInput(float deltaTime, bool reprocessing)
    {
        Notify("OnProcessInput", { ScriptDefinedRpcValue(deltaTime), ScriptDefinedRpcValue(reprocessing) });
    }
} // namespace ScriptCanvasMultiplayer
