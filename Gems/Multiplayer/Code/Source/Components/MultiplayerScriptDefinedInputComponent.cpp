/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/sort.h>
#include <Multiplayer/Components/MultiplayerScriptDefinedInputComponent.h>
#include <Multiplayer/Components/NetBindComponent.h>

namespace Multiplayer
{
    void MultiplayerScriptDefinedInputComponent::Reflect(AZ::ReflectContext* context)
    {
        ScriptDefinedField::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<MultiplayerScriptDefinedInputComponent, MultiplayerScriptDefinedInputComponentBase>()
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

    void MultiplayerScriptDefinedInputComponent::OnActivate([[maybe_unused]] EntityIsMigrating entityIsMigrating)
    {
        if (!ScriptDefinedInputRequestBus::Handler::BusIsConnected())
        {
            ScriptDefinedInputRequestBus::Handler::BusConnect(GetEntityId());
        }
    }

    void MultiplayerScriptDefinedInputComponent::OnDeactivate([[maybe_unused]] EntityIsMigrating entityIsMigrating)
    {
        ScriptDefinedInputRequestBus::Handler::BusDisconnect();
    }

    bool MultiplayerScriptDefinedInputComponent::ConfigureInputSchema(AZ::u64 schemaFingerprint, uint32_t valueCount)
    {
        if (m_configurationLocked || schemaFingerprint == 0 || valueCount == 0 || valueCount > ScriptDefinedInputCapacity)
        {
            return false;
        }

        m_schemaFingerprint = schemaFingerprint;
        m_valueCount = valueCount;
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

        // Frames hold values in ID order so that every peer agrees on the layout.
        m_fields = definition.m_inputs;
        AZStd::sort(
            m_fields.begin(),
            m_fields.end(),
            [](const ScriptDefinedField& lhs, const ScriptDefinedField& rhs)
            {
                return lhs.m_id < rhs.m_id;
            });
        m_valueCount = aznumeric_cast<uint32_t>(m_fields.size());
        return true;
    }

    bool MultiplayerScriptDefinedInputComponent::IsConfigured() const
    {
        if (m_schemaFingerprint == 0 || m_valueCount == 0 || m_valueCount > ScriptDefinedInputCapacity)
        {
            return false;
        }

        // An untyped layout has no fields; a typed one needs one field per value, each with a valid default.
        if (m_fields.empty())
        {
            return true;
        }

        if (m_fields.size() != m_valueCount)
        {
            return false;
        }

        return AZStd::all_of(
            m_fields.begin(),
            m_fields.end(),
            [](const ScriptDefinedField& field)
            {
                return ParseScriptDefinedValue(field.m_type, field.m_defaultValue).IsSuccess();
            });
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

        for (size_t index = 0; index < m_fields.size(); ++index)
        {
            if (m_fields[index].m_id == id && index < m_currentInput->m_typedValues.size())
            {
                value = m_currentInput->m_typedValues[index];
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

        ScriptDefinedRpcPayload finiteCheck;
        finiteCheck.m_arguments.push_back(value);
        if (!finiteCheck.IsValid())
        {
            return false;
        }

        for (size_t index = 0; index < m_fields.size(); ++index)
        {
            const ScriptDefinedField& field = m_fields[index];
            if (field.m_id == id && index < m_gatherInput->m_typedValues.size() && value.index() == static_cast<size_t>(field.m_type))
            {
                m_gatherInput->m_typedValues[index] = value;
                return true;
            }
        }
        return false;
    }

    bool MultiplayerScriptDefinedInputComponent::MatchesInputSchema(const ScriptDefinedInputFrame& frame) const
    {
        const bool expectsTypedFrame = !m_fields.empty();
        if (!IsConfigured() || frame.m_schemaFingerprint != m_schemaFingerprint || !frame.IsValid() || frame.m_isTyped != expectsTypedFrame)
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

        for (size_t index = 0; index < m_fields.size(); ++index)
        {
            if (frame.m_typedValues[index].index() != static_cast<size_t>(m_fields[index].m_type))
            {
                return false;
            }
        }
        return true;
    }

    void MultiplayerScriptDefinedInputComponent::CreateInputFrame(ScriptDefinedInputFrame& frame, float deltaTime)
    {
        // Start from the defaults of the configured layout.
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

        // Let handlers write a copy, readable and writable through the input request bus while they run.
        ScriptDefinedInputFrame candidate = frame;
        const ScriptDefinedInputFrame* previousRead = m_currentInput;
        ScriptDefinedInputFrame* previousWrite = m_gatherInput;
        m_currentInput = &candidate;
        m_gatherInput = &candidate;

        ScriptDefinedNotificationBus::Event(GetEntityId(), &ScriptDefinedNotifications::OnCreateInput, deltaTime);
        m_createInputEvent.Signal(candidate, deltaTime);

        m_currentInput = previousRead;
        m_gatherInput = previousWrite;

        // Keep the defaults if a handler broke the layout.
        if (MatchesInputSchema(candidate))
        {
            frame = AZStd::move(candidate);
        }
    }

    void MultiplayerScriptDefinedInputComponent::NetworkAttach(
        NetBindComponent* netBindComponent, ReplicationRecord& currentEntityRecord, ReplicationRecord& predictableEntityRecord)
    {
        m_configurationLocked = true;
        MultiplayerScriptDefinedInputComponentBase::NetworkAttach(netBindComponent, currentEntityRecord, predictableEntityRecord);
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
        if (!componentInput || !parent.MatchesInputSchema(componentInput->m_frame))
        {
            return;
        }

        // The processed frame is readable, but not writable, while handlers run.
        const ScriptDefinedInputFrame* previousRead = parent.m_currentInput;
        ScriptDefinedInputFrame* previousWrite = parent.m_gatherInput;
        parent.m_currentInput = &componentInput->m_frame;
        parent.m_gatherInput = nullptr;

        const bool reprocessing = GetNetBindComponent()->IsReprocessingInput();
        ScriptDefinedNotificationBus::Event(GetEntityId(), &ScriptDefinedNotifications::OnProcessInput, deltaTime, reprocessing);
        parent.m_processInputEvent.Signal(componentInput->m_frame, deltaTime, reprocessing);

        parent.m_currentInput = previousRead;
        parent.m_gatherInput = previousWrite;
    }
} // namespace Multiplayer
