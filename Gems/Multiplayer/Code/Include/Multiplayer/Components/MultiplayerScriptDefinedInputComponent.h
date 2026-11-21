/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <Multiplayer/MultiplayerScriptDefinedDefinition.h>
#include <Source/AutoGen/MultiplayerScriptDefinedInputComponent.AutoComponent.h>

namespace Multiplayer
{
    //! Optional input companion of MultiplayerScriptDefinedComponent, added only when a schema declares inputs.
    //! Configure its layout before it attaches to the network; its configuration is locked from then on.
    class MultiplayerScriptDefinedInputComponent
        : public MultiplayerScriptDefinedInputComponentBase
        , private ScriptDefinedInputRequestBus::Handler
    {
    public:
        AZ_MULTIPLAYER_COMPONENT(
            MultiplayerScriptDefinedInputComponent,
            s_multiplayerScriptDefinedInputComponentConcreteUuid,
            MultiplayerScriptDefinedInputComponentBase);

        //! Signaled while an input frame is created; handlers fill in the frame.
        using CreateInputEvent = AZ::Event<ScriptDefinedInputFrame&, float>;

        //! Signaled while an input frame is processed, with whether the input is being reprocessed.
        using ProcessInputEvent = AZ::Event<const ScriptDefinedInputFrame&, float, bool>;

        static void Reflect(AZ::ReflectContext* context);

        //! Configures an untyped layout of valueCount floats, for native callers.
        //! Returns false if the arguments are out of range or the configuration is locked.
        bool ConfigureInputSchema(AZ::u64 schemaFingerprint, uint32_t valueCount);

        //! Configures the typed layout of a definition's inputs.
        //! Returns false if the definition is invalid, has no inputs, or the configuration is locked.
        bool ConfigureDefinition(const MultiplayerScriptDefinedDefinition& definition);

        //! Returns true if a valid layout is configured. Only a configured component takes part in input processing.
        bool IsConfigured() const;

        //! @{
        //! Connect handlers to input creation and processing.
        void AddCreateInputHandler(CreateInputEvent::Handler& handler);
        void AddProcessInputHandler(ProcessInputEvent::Handler& handler);
        //! @}

        // MultiplayerComponent overrides
        void OnInit() override;
        void OnActivate(EntityIsMigrating entityIsMigrating) override;
        void OnDeactivate(EntityIsMigrating entityIsMigrating) override;

        // ScriptDefinedInputRequestBus::Handler overrides
        bool ReadInput(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const override;
        bool WriteInput(const AZ::Uuid& id, const ScriptDefinedRpcValue& value) override;

    protected:
        // MultiplayerComponent overrides
        void NetworkAttach(
            NetBindComponent* netBindComponent,
            ReplicationRecord& currentEntityRecord,
            ReplicationRecord& predictableEntityRecord) override;

    private:
        friend class MultiplayerScriptDefinedInputComponentController;

        bool MatchesInputSchema(const ScriptDefinedInputFrame& frame) const;

        //! Fills the frame with defaults and lets handlers write it. The frame keeps its defaults if a handler breaks the layout.
        void CreateInputFrame(ScriptDefinedInputFrame& frame, float deltaTime);

        AZ::u64 m_schemaFingerprint = 0;
        uint32_t m_valueCount = 0;
        bool m_configurationLocked = false;

        //! Typed input fields sorted by ID; empty for an untyped layout.
        AZStd::vector<ScriptDefinedField> m_fields;

        //! Frame that ReadInput reads, set only while input is created or processed.
        const ScriptDefinedInputFrame* m_currentInput = nullptr;

        //! Frame that WriteInput writes, set only while input is created.
        ScriptDefinedInputFrame* m_gatherInput = nullptr;

        CreateInputEvent m_createInputEvent;
        ProcessInputEvent m_processInputEvent;
    };

    //! Controller of MultiplayerScriptDefinedInputComponent. It creates and processes input only when its component is configured.
    class MultiplayerScriptDefinedInputComponentController : public MultiplayerScriptDefinedInputComponentControllerBase
    {
    public:
        explicit MultiplayerScriptDefinedInputComponentController(MultiplayerScriptDefinedInputComponent& parent)
            : MultiplayerScriptDefinedInputComponentControllerBase(parent)
        {
        }

        // MultiplayerController overrides
        void OnActivate([[maybe_unused]] EntityIsMigrating entityIsMigrating) override
        {
        }

        void OnDeactivate([[maybe_unused]] EntityIsMigrating entityIsMigrating) override
        {
        }

        bool IsInputEnabled() const override
        {
            return GetParent().IsConfigured();
        }

        void CreateInput(NetworkInput& input, float deltaTime) override;
        void ProcessInput(NetworkInput& input, float deltaTime) override;
    };
} // namespace Multiplayer
