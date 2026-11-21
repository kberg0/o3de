/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
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

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<MultiplayerScriptDefinedBridge, AZ::Component>()->Version(1)->Field(
                "Interface", &MultiplayerScriptDefinedBridge::m_interfaceAsset);
        }
    }

    void MultiplayerScriptDefinedBridge::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("MultiplayerScriptDefinedComponent"));
    }

    MultiplayerScriptDefinedBridge::~MultiplayerScriptDefinedBridge()
    {
        Unbind();
    }

    void MultiplayerScriptDefinedBridge::Configure(AZ::Data::Asset<ScriptEvents::ScriptEventsAsset> interfaceAsset)
    {
        m_interfaceAsset = AZStd::move(interfaceAsset);
    }

    void MultiplayerScriptDefinedBridge::Init()
    {
        // Binding early lets graphs that activate before this component find the interface already connected.
        Bind();
    }

    void MultiplayerScriptDefinedBridge::Activate()
    {
        if (!Bind())
        {
            AZ_Error("Multiplayer", false, "Cannot bind Script Defined interface for %s", m_interfaceAsset.GetHint().c_str());
        }
    }

    void MultiplayerScriptDefinedBridge::Deactivate()
    {
        ScriptDefinedNotificationBus::Handler::BusDisconnect();
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

        // The interface product must have been generated from the same schema revision.
        const ScriptEvents::ScriptEvent expected = MakeScriptDefinedInterface(*m_definition);
        const ScriptEvents::ScriptEvent& actual = m_interfaceAsset->m_definition;
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

        // Route every graph call to Dispatch; notifications are sent by this component instead.
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
        void* instance,
        const char* name,
        [[maybe_unused]] int eventIndex,
        AZ::BehaviorArgument* result,
        int count,
        AZ::BehaviorArgument* arguments)
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

        // Getters return the value itself.
        const bool isGetter = method->m_operation == ScriptDefinedOperation::GetProperty ||
            method->m_operation == ScriptDefinedOperation::GetInput || method->m_operation == ScriptDefinedOperation::GetArchetype;

        AZStd::fixed_vector<ScriptDefinedRpcValue, ScriptDefinedArgumentCapacity> values;
        bool argumentsValid = true;
        for (int index = 0; index < count; ++index)
        {
            ScriptDefinedRpcValue value;
            if (!ReadScriptDefinedArgument(method->m_parameters[index].m_type, arguments[index], value))
            {
                argumentsValid = false;
                break;
            }
            values.push_back(AZStd::move(value));
        }

        if (argumentsValid && isGetter)
        {
            const ScriptDefinedField* field = nullptr;
            if (method->m_operation == ScriptDefinedOperation::GetProperty)
            {
                field = m_definition->FindProperty(method->m_memberId);
            }
            else if (method->m_operation == ScriptDefinedOperation::GetArchetype)
            {
                field = m_definition->FindArchetype(method->m_memberId);
            }

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

            ScriptDefinedRpcValue value = DefaultScriptDefinedValue(field->m_type);
            bool success = false;
            if (method->m_operation == ScriptDefinedOperation::GetProperty)
            {
                ScriptDefinedRequestBus::EventResult(success, GetEntityId(), &ScriptDefinedRequests::ReadProperty, field->m_id, value);
            }
            else if (method->m_operation == ScriptDefinedOperation::GetInput)
            {
                ScriptDefinedInputRequestBus::EventResult(
                    success, GetEntityId(), &ScriptDefinedInputRequests::ReadInput, field->m_id, value);
            }
            else
            {
                ScriptDefinedRequestBus::EventResult(success, GetEntityId(), &ScriptDefinedRequests::ReadArchetype, field->m_id, value);
            }

            // A failed read returns the type's default.
            if (result)
            {
                StoreScriptDefinedResult(value, *result);
            }
            return;
        }

        // Setters and RPC sends return whether they succeeded.
        bool success = false;
        if (argumentsValid)
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
                break;
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

        // The first argument is the bus address; the values follow, converted to their pin types.
        AZStd::fixed_vector<AZStd::any, ScriptDefinedArgumentCapacity> storage;
        AZStd::array<AZ::BehaviorArgument, ScriptDefinedArgumentCapacity + 1> arguments;
        AZ::EntityId entityId = GetEntityId();
        arguments[0].Set(&entityId);

        storage.resize(values.size());
        for (size_t index = 0; index < values.size(); ++index)
        {
            storage[index] = ToScriptPinValue(values[index]);
            AZ::BehaviorObject pinValue(AZStd::any_cast<void>(&storage[index]), storage[index].type());
            arguments[index + 1].Set(AZ::BehaviorArgumentValueTypeTag, &pinValue);
        }

        found->second.m_event->Call(arguments.data(), aznumeric_cast<unsigned>(values.size() + 1));
    }

    void MultiplayerScriptDefinedBridge::OnPropertyChanged(const AZ::Uuid& id, const ScriptDefinedRpcValue& value)
    {
        if (const ScriptDefinedField* field = m_definition->FindProperty(id))
        {
            Notify(ScriptDefinedPropertyChangedPrefix + field->m_name, { value });
        }
    }

    void MultiplayerScriptDefinedBridge::OnRpcReceived(uint16_t rpcId, const ScriptDefinedRpcPayload& payload)
    {
        for (const auto& rpc : m_definition->m_rpcs)
        {
            if (m_definition->GetRpcId(rpc.m_id) == rpcId)
            {
                Notify(ScriptDefinedRpcReceivedPrefix + rpc.m_name, payload.m_arguments);
                return;
            }
        }
    }

    void MultiplayerScriptDefinedBridge::OnCreateInput(float deltaTime)
    {
        Notify(ScriptDefinedCreateInputEvent, { ScriptDefinedRpcValue(deltaTime) });
    }

    void MultiplayerScriptDefinedBridge::OnProcessInput(float deltaTime, bool reprocessing)
    {
        Notify(ScriptDefinedProcessInputEvent, { ScriptDefinedRpcValue(deltaTime), ScriptDefinedRpcValue(reprocessing) });
    }
} // namespace ScriptCanvasMultiplayer
