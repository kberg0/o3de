/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <Multiplayer/MultiplayerScriptDefinedDefinition.h>
#include <Source/AutoGen/MultiplayerScriptDefinedInputComponent.AutoComponent.h>

namespace Multiplayer
{
    //! Optional input companion; configure its layout before network attachment.
    class MultiplayerScriptDefinedInputComponent
        : public MultiplayerScriptDefinedInputComponentBase
        , private ScriptDefinedInputRequestBus::Handler
    {
    public:
        AZ_MULTIPLAYER_COMPONENT(
            MultiplayerScriptDefinedInputComponent,
            s_multiplayerScriptDefinedInputComponentConcreteUuid,
            MultiplayerScriptDefinedInputComponentBase);
        static void Reflect(AZ::ReflectContext* context);
        void OnInit() override;
        void OnActivate(EntityIsMigrating) override;
        void OnDeactivate(EntityIsMigrating) override;

        using CreateInputEvent = AZ::Event<ScriptDefinedInputFrame&, float>;
        using ProcessInputEvent = AZ::Event<const ScriptDefinedInputFrame&, float, bool>;

        bool ConfigureInputSchema(AZ::u64 schemaFingerprint, uint32_t valueCount);
        bool ConfigureDefinition(const MultiplayerScriptDefinedDefinition& definition);
        bool ReadInput(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const override;
        bool WriteInput(const AZ::Uuid& id, const ScriptDefinedRpcValue& value) override;
        bool IsConfigured() const;
        void AddCreateInputHandler(CreateInputEvent::Handler& handler);
        void AddProcessInputHandler(ProcessInputEvent::Handler& handler);

    protected:
        void NetworkAttach(
            NetBindComponent* netBindComponent,
            ReplicationRecord& currentEntityRecord,
            ReplicationRecord& predictableEntityRecord) override;

    private:
        friend class MultiplayerScriptDefinedInputComponentController;
        bool MatchesInputSchema(const ScriptDefinedInputFrame& frame) const;
        void CreateInputFrame(ScriptDefinedInputFrame& frame, float deltaTime);

        AZ::u64 m_schemaFingerprint = 0;
        uint32_t m_valueCount = 0;
        bool m_configurationLocked = false;
        AZStd::vector<ScriptDefinedField> m_fields;
        const ScriptDefinedInputFrame* m_currentInput = nullptr;
        ScriptDefinedInputFrame* m_gatherInput = nullptr;
        CreateInputEvent m_createInputEvent;
        ProcessInputEvent m_processInputEvent;
    };

    class MultiplayerScriptDefinedInputComponentController : public MultiplayerScriptDefinedInputComponentControllerBase
    {
    public:
        explicit MultiplayerScriptDefinedInputComponentController(MultiplayerScriptDefinedInputComponent& parent)
            : MultiplayerScriptDefinedInputComponentControllerBase(parent)
        {
        }
        void OnActivate(EntityIsMigrating) override
        {
        }
        void OnDeactivate(EntityIsMigrating) override
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
