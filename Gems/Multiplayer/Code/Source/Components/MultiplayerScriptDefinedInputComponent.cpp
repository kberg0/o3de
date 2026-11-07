/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/sort.h>
#include <Multiplayer/Components/MultiplayerScriptDefinedInputComponent.h>
#include <Multiplayer/Components/NetBindComponent.h>

namespace Multiplayer
{
    void MultiplayerScriptDefinedInputComponent::Reflect(AZ::ReflectContext* context)
    {
        ScriptDefinedField::Reflect(context);
        if (auto* sc = azrtti_cast<AZ::SerializeContext*>(context))
        {
            sc->Class<MultiplayerScriptDefinedInputComponent, MultiplayerScriptDefinedInputComponentBase>()
                ->Version(2)
                ->Field("SchemaFingerprint", &MultiplayerScriptDefinedInputComponent::m_schemaFingerprint)
                ->Field("ValueCount", &MultiplayerScriptDefinedInputComponent::m_valueCount)
                ->Field("Fields", &MultiplayerScriptDefinedInputComponent::m_fields);
        }
        // The generated base reflects the concrete class into EditContext.
        MultiplayerScriptDefinedInputComponentBase::Reflect(context);
    }
    void MultiplayerScriptDefinedInputComponent::OnInit()
    {
        ScriptDefinedInputRequestBus::Handler::BusConnect(GetEntityId());
    }
    void MultiplayerScriptDefinedInputComponent::OnActivate(EntityIsMigrating)
    {
        if (!ScriptDefinedInputRequestBus::Handler::BusIsConnected())
        {
            ScriptDefinedInputRequestBus::Handler::BusConnect(GetEntityId());
        }
    }
    void MultiplayerScriptDefinedInputComponent::OnDeactivate(EntityIsMigrating)
    {
        ScriptDefinedInputRequestBus::Handler::BusDisconnect();
    }
    bool MultiplayerScriptDefinedInputComponent::ConfigureInputSchema(AZ::u64 fingerprint, uint32_t count)
    {
        if (m_configurationLocked || fingerprint == 0 || count == 0 || count > ScriptDefinedInputCapacity)
        {
            return false;
        }
        m_schemaFingerprint = fingerprint;
        m_valueCount = count;
        m_fields.clear();
        return true;
    }
    bool MultiplayerScriptDefinedInputComponent::ConfigureDefinition(const MultiplayerScriptDefinedDefinition& definition)
    {
        if (m_configurationLocked || !definition.Validate() || definition.m_inputs.empty())
        {
            return false;
        }
        m_schemaFingerprint = definition.GetFingerprint();
        m_fields = definition.m_inputs;
        AZStd::sort(
            m_fields.begin(),
            m_fields.end(),
            [](const auto& a, const auto& b)
            {
                return a.m_id < b.m_id;
            });
        m_valueCount = aznumeric_cast<uint32_t>(m_fields.size());
        return true;
    }
    bool MultiplayerScriptDefinedInputComponent::IsConfigured() const
    {
        if (!m_schemaFingerprint || !m_valueCount || m_valueCount > ScriptDefinedInputCapacity)
        {
            return false;
        }
        if (!m_fields.empty())
        {
            if (m_fields.size() != m_valueCount)
            {
                return false;
            }
            for (const auto& field : m_fields)
            {
                if (!ParseScriptDefinedValue(field.m_type, field.m_defaultValue))
                {
                    return false;
                }
            }
        }
        return true;
    }
    void MultiplayerScriptDefinedInputComponent::AddCreateInputHandler(CreateInputEvent::Handler& handler)
    {
        handler.Connect(m_createInputEvent);
    }
    void MultiplayerScriptDefinedInputComponent::AddProcessInputHandler(ProcessInputEvent::Handler& handler)
    {
        handler.Connect(m_processInputEvent);
    }

    bool MultiplayerScriptDefinedInputComponent::ReadInput(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const
    {
        if (!m_currentInput || !m_currentInput->m_isTyped)
        {
            return false;
        }
        for (size_t i = 0; i < m_fields.size(); ++i)
        {
            if (m_fields[i].m_id == id && i < m_currentInput->m_typedValues.size())
            {
                value = m_currentInput->m_typedValues[i];
                return true;
            }
        }
        return false;
    }
    bool MultiplayerScriptDefinedInputComponent::WriteInput(const AZ::Uuid& id, const ScriptDefinedRpcValue& value)
    {
        if (!m_gatherInput || !m_gatherInput->m_isTyped)
        {
            return false;
        }
        ScriptDefinedRpcPayload check;
        check.m_arguments.push_back(value);
        if (!check.IsValid())
        {
            return false;
        }
        for (size_t i = 0; i < m_fields.size(); ++i)
        {
            if (m_fields[i].m_id == id && i < m_gatherInput->m_typedValues.size() &&
                value.index() == static_cast<size_t>(m_fields[i].m_type))
            {
                m_gatherInput->m_typedValues[i] = value;
                return true;
            }
        }
        return false;
    }
    bool MultiplayerScriptDefinedInputComponent::MatchesInputSchema(const ScriptDefinedInputFrame& frame) const
    {
        if (!IsConfigured() || frame.m_schemaFingerprint != m_schemaFingerprint || !frame.IsValid() || frame.m_isTyped != !m_fields.empty())
        {
            return false;
        }
        if (!frame.m_isTyped)
        {
            return frame.m_values.size() == m_valueCount && frame.m_typedValues.empty();
        }
        if (frame.m_typedValues.size() != m_valueCount || !frame.m_values.empty())
        {
            return false;
        }
        for (size_t i = 0; i < m_fields.size(); ++i)
        {
            if (frame.m_typedValues[i].index() != static_cast<size_t>(m_fields[i].m_type))
            {
                return false;
            }
        }
        return true;
    }
    void MultiplayerScriptDefinedInputComponent::CreateInputFrame(ScriptDefinedInputFrame& frame, float deltaTime)
    {
        frame = {};
        frame.m_schemaFingerprint = m_schemaFingerprint;
        frame.m_isTyped = !m_fields.empty();
        if (frame.m_isTyped)
        {
            for (const auto& field : m_fields)
            {
                frame.m_typedValues.push_back(ParseScriptDefinedValue(field.m_type, field.m_defaultValue).TakeValue());
            }
        }
        else
        {
            frame.m_values.resize(m_valueCount, 0.0f);
        }
        auto candidate = frame;
        auto* previousRead = m_currentInput;
        auto* previousWrite = m_gatherInput;
        m_currentInput = &candidate;
        m_gatherInput = &candidate;
        ScriptDefinedNotificationBus::Event(GetEntityId(), &ScriptDefinedNotifications::OnCreateInput, deltaTime);
        m_createInputEvent.Signal(candidate, deltaTime);
        m_currentInput = previousRead;
        m_gatherInput = previousWrite;
        if (MatchesInputSchema(candidate))
        {
            frame = AZStd::move(candidate);
        }
    }
    void MultiplayerScriptDefinedInputComponent::NetworkAttach(
        NetBindComponent* binding, ReplicationRecord& current, ReplicationRecord& predictable)
    {
        m_configurationLocked = true;
        MultiplayerScriptDefinedInputComponentBase::NetworkAttach(binding, current, predictable);
    }
    void MultiplayerScriptDefinedInputComponentController::CreateInput(NetworkInput& input, float deltaTime)
    {
        if (!GetParent().IsConfigured())
        {
            return;
        }
        if (auto* componentInput = input.FindComponentInput<MultiplayerScriptDefinedInputComponentNetworkInput>())
        {
            GetParent().CreateInputFrame(componentInput->m_frame, deltaTime);
        }
    }
    void MultiplayerScriptDefinedInputComponentController::ProcessInput(NetworkInput& input, float deltaTime)
    {
        const auto* componentInput = input.FindComponentInput<MultiplayerScriptDefinedInputComponentNetworkInput>();
        auto& parent = GetParent();
        if (componentInput && parent.MatchesInputSchema(componentInput->m_frame))
        {
            auto* previousRead = parent.m_currentInput;
            auto* previousWrite = parent.m_gatherInput;
            parent.m_currentInput = &componentInput->m_frame;
            parent.m_gatherInput = nullptr;
            const bool reprocessing = GetNetBindComponent()->IsReprocessingInput();
            ScriptDefinedNotificationBus::Event(GetEntityId(), &ScriptDefinedNotifications::OnProcessInput, deltaTime, reprocessing);
            parent.m_processInputEvent.Signal(componentInput->m_frame, deltaTime, reprocessing);
            parent.m_currentInput = previousRead;
            parent.m_gatherInput = previousWrite;
        }
    }
} // namespace Multiplayer
