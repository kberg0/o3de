/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Script/ScriptContextAttributes.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/parallel/thread.h>
#include <EMotionFX/CommandSystem/Source/CommandManager.h>
#include <EMotionFX/Source/EMotionFXManager.h>
#include <EMotionFX/Source/MotionManager.h>
#include <Integration/AnimationBus.h>
#include <Integration/Components/ActorComponent.h>
#include <Integration/Components/AnimGraphComponent.h>
#include <Integration/Components/SimpleMotionComponent.h>
#include <Integration/Editor/Components/EditorSimpleMotionComponent.h>
#include <Integration/Editor/EditorAutomationHandler.h>
#include <Integration/EditorAutomationBus.h>
#include <Tests/UI/UIFixture.h>

namespace EMotionFX
{
    namespace
    {
        using Integration::EditorAutomationRequestBus;
        using Integration::EditorAutomationRequests;
        using AutomationScopeFlags = AZ::Script::Attributes::ScopeFlags;

        //! An undoable command, and the name of the motion set it creates.
        constexpr const char* EditorAutomationTestCommand = "CreateMotionSet -name EditorAutomationTestSet";
        constexpr const char* EditorAutomationTestSetName = "EditorAutomationTestSet";

        //! Reads a bus's scope the way EditorPythonBindings does: without a Scope attribute a bus is Launcher only.
        AutomationScopeFlags ReadBusScope(const AZ::BehaviorEBus& bus)
        {
            AutomationScopeFlags scope = AutomationScopeFlags::Launcher;
            if (AZ::Attribute* scopeAttribute = AZ::FindAttribute(AZ::Script::Attributes::Scope, bus.m_attributes))
            {
                AZ::AttributeReader(nullptr, scopeAttribute).Read<AutomationScopeFlags>(scope);
            }
            return scope;
        }

        //! Reads a bus's script module the way EditorPythonBindings does.
        AZStd::string ReadBusModule(const AZ::BehaviorEBus& bus)
        {
            const char* moduleName = nullptr;
            if (AZ::Attribute* moduleAttribute = AZ::FindAttribute(AZ::Script::Attributes::Module, bus.m_attributes))
            {
                AZ::AttributeReader(nullptr, moduleAttribute).Read<const char*>(moduleName);
            }
            return moduleName ? AZStd::string(moduleName) : AZStd::string();
        }

        EditorAutomationRequests::CommandOutcome ExecuteAutomationCommand(const AZStd::string& command)
        {
            EditorAutomationRequests::CommandOutcome outcome;
            EditorAutomationRequestBus::BroadcastResult(outcome, &EditorAutomationRequests::ExecuteCommand, command);
            return outcome;
        }

        //! A default-constructed outcome is a failure with an empty error, so a non-empty error shows the handler answered.
        void ExpectHandlerRefused(const EditorAutomationRequests::CommandOutcome& outcome)
        {
            ASSERT_FALSE(outcome.IsSuccess());
            EXPECT_FALSE(outcome.GetError().empty());
        }
    } // namespace

    class EditorAutomationFixture : public UIFixture
    {
    protected:
        void ExpectBusScope(const char* busName, AutomationScopeFlags expectedScope, const char* expectedModule)
        {
            const AZ::BehaviorContext* behaviorContext = m_app.GetBehaviorContext();
            ASSERT_NE(behaviorContext, nullptr);

            const auto busIterator = behaviorContext->m_ebuses.find(busName);
            ASSERT_NE(busIterator, behaviorContext->m_ebuses.end()) << busName << " is not reflected";
            EXPECT_EQ(ReadBusScope(*busIterator->second), expectedScope) << busName;
            EXPECT_EQ(ReadBusModule(*busIterator->second), expectedModule) << busName;
        }
    };

    TEST_F(EditorAutomationFixture, BusesAreReflectedForEditorAutomation)
    {
        m_app.RegisterComponentDescriptor(Integration::ActorComponent::CreateDescriptor());
        m_app.RegisterComponentDescriptor(Integration::AnimGraphComponent::CreateDescriptor());
        m_app.RegisterComponentDescriptor(Integration::SimpleMotionComponent::CreateDescriptor());
        m_app.RegisterComponentDescriptor(Integration::EditorSimpleMotionComponent::CreateDescriptor());

        // Common keeps the buses available to Lua and Script Canvas and adds Editor automation.
        ExpectBusScope("ActorComponentRequestBus", AutomationScopeFlags::Common, Integration::ScriptModule);
        ExpectBusScope("AnimGraphComponentRequestBus", AutomationScopeFlags::Common, Integration::ScriptModule);
        ExpectBusScope("SimpleMotionComponentRequestBus", AutomationScopeFlags::Common, Integration::ScriptModule);
        ExpectBusScope("ActorNotificationBus", AutomationScopeFlags::Common, Integration::ScriptModule);

        ExpectBusScope("EditorSimpleMotionComponentRequestBus", AutomationScopeFlags::Automation, Integration::EditorScriptModule);
        ExpectBusScope("EMotionFXEditorAutomationRequestBus", AutomationScopeFlags::Automation, Integration::EditorScriptModule);
    }

    TEST_F(EditorAutomationFixture, ExecutedCommandCanBeUndoneAndRedone)
    {
        const size_t historySizeBefore = CommandSystem::GetCommandManager()->GetNumHistoryItems();

        const EditorAutomationRequests::CommandOutcome executed = ExecuteAutomationCommand(EditorAutomationTestCommand);
        ASSERT_TRUE(executed.IsSuccess()) << executed.GetError().c_str();
        EXPECT_EQ(CommandSystem::GetCommandManager()->GetNumHistoryItems(), historySizeBefore + 1);
        EXPECT_NE(GetMotionManager().FindMotionSetByName(EditorAutomationTestSetName), nullptr);

        EditorAutomationRequests::CommandOutcome undone;
        EditorAutomationRequestBus::BroadcastResult(undone, &EditorAutomationRequests::Undo);
        ASSERT_TRUE(undone.IsSuccess()) << undone.GetError().c_str();
        EXPECT_EQ(GetMotionManager().FindMotionSetByName(EditorAutomationTestSetName), nullptr);

        EditorAutomationRequests::CommandOutcome redone;
        EditorAutomationRequestBus::BroadcastResult(redone, &EditorAutomationRequests::Redo);
        ASSERT_TRUE(redone.IsSuccess()) << redone.GetError().c_str();
        EXPECT_NE(GetMotionManager().FindMotionSetByName(EditorAutomationTestSetName), nullptr);
    }

    TEST_F(EditorAutomationFixture, UndoAndRedoFailWhenHistoryIsEmpty)
    {
        CommandSystem::GetCommandManager()->ClearHistory();

        EditorAutomationRequests::CommandOutcome undone;
        EditorAutomationRequestBus::BroadcastResult(undone, &EditorAutomationRequests::Undo);
        ExpectHandlerRefused(undone);

        EditorAutomationRequests::CommandOutcome redone;
        EditorAutomationRequestBus::BroadcastResult(redone, &EditorAutomationRequests::Redo);
        ExpectHandlerRefused(redone);
    }

    TEST_F(EditorAutomationFixture, BadCommandReportsError)
    {
        const size_t historySizeBefore = CommandSystem::GetCommandManager()->GetNumHistoryItems();

        ExpectHandlerRefused(ExecuteAutomationCommand("NoSuchEMotionFXCommand -name Test"));
        ExpectHandlerRefused(ExecuteAutomationCommand("CreateMotionSet"));

        EXPECT_EQ(CommandSystem::GetCommandManager()->GetNumHistoryItems(), historySizeBefore);
    }

    TEST_F(EditorAutomationFixture, CommandOverLengthLimitIsRefused)
    {
        const AZStd::string tooLong(Integration::EditorAutomationHandler::MaxCommandLength + 1, 'a');

        ExpectHandlerRefused(ExecuteAutomationCommand(tooLong));

        EditorAutomationRequests::CommandOutcome syntax;
        EditorAutomationRequestBus::BroadcastResult(syntax, &EditorAutomationRequests::GetCommandSyntax, tooLong);
        ExpectHandlerRefused(syntax);
    }

    TEST_F(EditorAutomationFixture, HelpOnCommandWithoutParametersSucceeds)
    {
        // ResetToBindPose has no parameters, which used to make CommandSyntax::LogSyntax() read past its parameter list.
        EXPECT_TRUE(ExecuteAutomationCommand("ResetToBindPose -help").IsSuccess());
    }

    TEST_F(EditorAutomationFixture, ListsCommandsAndDescribesSyntax)
    {
        EditorAutomationRequests::CommandNamesOutcome names;
        EditorAutomationRequestBus::BroadcastResult(names, &EditorAutomationRequests::GetCommandNames);
        ASSERT_TRUE(names.IsSuccess()) << names.GetError().c_str();

        const AZStd::vector<AZStd::string>& commandNames = names.GetValue();
        EXPECT_TRUE(AZStd::is_sorted(commandNames.begin(), commandNames.end()));
        EXPECT_NE(AZStd::find(commandNames.begin(), commandNames.end(), "CreateMotionSet"), commandNames.end());

        EditorAutomationRequests::CommandOutcome syntax;
        EditorAutomationRequestBus::BroadcastResult(syntax, &EditorAutomationRequests::GetCommandSyntax, AZStd::string("CreateMotionSet"));
        ASSERT_TRUE(syntax.IsSuccess()) << syntax.GetError().c_str();
        EXPECT_NE(syntax.GetValue().find("-name (String, required)"), AZStd::string::npos) << syntax.GetValue().c_str();

        EditorAutomationRequests::CommandOutcome unknown;
        EditorAutomationRequestBus::BroadcastResult(
            unknown, &EditorAutomationRequests::GetCommandSyntax, AZStd::string("NoSuchEMotionFXCommand"));
        ExpectHandlerRefused(unknown);
    }

    TEST_F(EditorAutomationFixture, RequestFromOtherThreadIsRefused)
    {
        EditorAutomationRequests::CommandOutcome outcome;
        AZStd::thread worker(
            [&outcome]()
            {
                outcome = ExecuteAutomationCommand(EditorAutomationTestCommand);
            });
        worker.join();

        ExpectHandlerRefused(outcome);
        EXPECT_EQ(GetMotionManager().FindMotionSetByName(EditorAutomationTestSetName), nullptr);
    }
} // namespace EMotionFX
