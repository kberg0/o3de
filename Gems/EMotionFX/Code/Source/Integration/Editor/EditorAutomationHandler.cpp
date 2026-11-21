/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Casting/numeric_cast.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/std/sort.h>
#include <EMotionFX/CommandSystem/Source/CommandManager.h>
#include <Integration/AnimationBus.h>
#include <Integration/Editor/EditorAutomationHandler.h>
#include <MCore/Source/Command.h>
#include <MCore/Source/CommandSyntax.h>

namespace EMotionFX
{
    namespace Integration
    {
        void EditorAutomationHandler::Reflect(AZ::ReflectContext* context)
        {
            auto* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context);
            if (!behaviorContext)
            {
                return;
            }

            behaviorContext->EBus<EditorAutomationRequestBus>("EMotionFXEditorAutomationRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation)
                ->Attribute(AZ::Script::Attributes::Module, EditorScriptModule)
                ->Attribute(
                    AZ::Script::Attributes::ToolTip,
                    "Executes, lists, describes, undoes and redoes EMotion FX commands, the undoable edits of the Animation Editor.")
                ->Event("ExecuteCommand", &EditorAutomationRequestBus::Events::ExecuteCommand)
                ->Attribute(
                    AZ::Script::Attributes::ToolTip,
                    "Executes one command string, for example 'CreateMotionSet -name Locomotion', and records it for undo. "
                    "Returns the command's result text, or the error.")
                ->Event("GetCommandNames", &EditorAutomationRequestBus::Events::GetCommandNames)
                ->Attribute(AZ::Script::Attributes::ToolTip, "Returns the names of all registered commands, sorted.")
                ->Event("GetCommandSyntax", &EditorAutomationRequestBus::Events::GetCommandSyntax)
                ->Attribute(AZ::Script::Attributes::ToolTip, "Returns a command's description and its parameters, one per line.")
                ->Event("Undo", &EditorAutomationRequestBus::Events::Undo)
                ->Attribute(AZ::Script::Attributes::ToolTip, "Undoes the most recent command.")
                ->Event("Redo", &EditorAutomationRequestBus::Events::Redo)
                ->Attribute(AZ::Script::Attributes::ToolTip, "Redoes the most recently undone command.");
        }

        void EditorAutomationHandler::Activate()
        {
            m_mainThreadId = AZStd::this_thread::get_id();
            EditorAutomationRequestBus::Handler::BusConnect();
        }

        void EditorAutomationHandler::Deactivate()
        {
            EditorAutomationRequestBus::Handler::BusDisconnect();
        }

        EditorAutomationRequests::CommandOutcome EditorAutomationHandler::ExecuteCommand(const AZStd::string& command)
        {
            auto commandManager = GetCommandManagerForRequest();
            if (!commandManager.IsSuccess())
            {
                return AZ::Failure(commandManager.TakeError());
            }

            if (command.size() > MaxCommandLength)
            {
                return AZ::Failure(
                    AZStd::string::format("The command is %zu characters long, over the limit of %zu.", command.size(), MaxCommandLength));
            }

            // Errors are returned to the caller instead of opening the Animation Editor's error report window.
            AZStd::string result;
            const bool succeeded = commandManager.GetValue()->ExecuteCommand(
                command,
                result,
                /*addToHistory=*/true,
                /*outExecutedCommand=*/nullptr,
                /*outExecutedParameters=*/nullptr,
                /*callFromCommandGroup=*/false,
                /*clearErrors=*/true,
                /*handleErrors=*/false);

            if (!succeeded)
            {
                return AZ::Failure(result.empty() ? AZStd::string("The command failed.") : result);
            }

            return AZ::Success(result);
        }

        EditorAutomationRequests::CommandNamesOutcome EditorAutomationHandler::GetCommandNames()
        {
            auto commandManager = GetCommandManagerForRequest();
            if (!commandManager.IsSuccess())
            {
                return AZ::Failure(commandManager.TakeError());
            }

            CommandSystem::CommandManager* manager = commandManager.GetValue();
            const size_t numCommands = manager->GetNumRegisteredCommands();

            AZStd::vector<AZStd::string> commandNames;
            commandNames.reserve(numCommands);
            for (size_t commandIndex = 0; commandIndex < numCommands; ++commandIndex)
            {
                commandNames.push_back(manager->GetCommand(commandIndex)->GetNameString());
            }

            AZStd::sort(commandNames.begin(), commandNames.end());
            return AZ::Success(AZStd::move(commandNames));
        }

        EditorAutomationRequests::CommandOutcome EditorAutomationHandler::GetCommandSyntax(const AZStd::string& commandName)
        {
            auto commandManager = GetCommandManagerForRequest();
            if (!commandManager.IsSuccess())
            {
                return AZ::Failure(commandManager.TakeError());
            }

            if (commandName.size() > MaxCommandLength)
            {
                return AZ::Failure(AZStd::string::format(
                    "The command name is %zu characters long, over the limit of %zu.", commandName.size(), MaxCommandLength));
            }

            MCore::Command* command = commandManager.GetValue()->FindCommand(commandName);
            if (!command)
            {
                return AZ::Failure(AZStd::string::format("There is no command named '%s'.", commandName.c_str()));
            }

            // First line: the name and description. Then one line per parameter, in the form used on the command line.
            AZStd::string syntaxText = command->GetNameString();
            const char* description = command->GetDescription();
            if (description && description[0] != '\0')
            {
                syntaxText += ": ";
                syntaxText += description;
            }

            const MCore::CommandSyntax& syntax = command->GetSyntax();
            for (size_t parameterIndex = 0; parameterIndex < syntax.GetNumParameters(); ++parameterIndex)
            {
                const AZStd::string requirement = syntax.GetParamRequired(parameterIndex)
                    ? AZStd::string("required")
                    : AZStd::string::format("default '%s'", syntax.GetDefaultValue(parameterIndex).c_str());

                syntaxText += AZStd::string::format(
                    "\n-%s (%s, %s): %s",
                    syntax.GetParamName(parameterIndex),
                    syntax.GetParamTypeString(parameterIndex),
                    requirement.c_str(),
                    syntax.GetParamDescription(parameterIndex));
            }

            return AZ::Success(syntaxText);
        }

        EditorAutomationRequests::CommandOutcome EditorAutomationHandler::Undo()
        {
            auto commandManager = GetCommandManagerForRequest();
            if (!commandManager.IsSuccess())
            {
                return AZ::Failure(commandManager.TakeError());
            }

            // MCore::CommandManager::Undo() does not check that there is something to undo.
            CommandSystem::CommandManager* manager = commandManager.GetValue();
            if (manager->GetHistoryIndex() < 0)
            {
                return AZ::Failure(AZStd::string("There is no command to undo."));
            }

            AZStd::string result;
            if (!manager->Undo(result))
            {
                return AZ::Failure(result.empty() ? AZStd::string("Undo failed.") : result);
            }

            return AZ::Success(result);
        }

        EditorAutomationRequests::CommandOutcome EditorAutomationHandler::Redo()
        {
            auto commandManager = GetCommandManagerForRequest();
            if (!commandManager.IsSuccess())
            {
                return AZ::Failure(commandManager.TakeError());
            }

            // MCore::CommandManager::Redo() does not check that there is something to redo.
            CommandSystem::CommandManager* manager = commandManager.GetValue();
            const ptrdiff_t nextHistoryIndex = manager->GetHistoryIndex() + 1;
            if (nextHistoryIndex >= aznumeric_cast<ptrdiff_t>(manager->GetNumHistoryItems()))
            {
                return AZ::Failure(AZStd::string("There is no command to redo."));
            }

            AZStd::string result;
            if (!manager->Redo(result))
            {
                return AZ::Failure(result.empty() ? AZStd::string("Redo failed.") : result);
            }

            return AZ::Success(result);
        }

        AZ::Outcome<CommandSystem::CommandManager*, AZStd::string> EditorAutomationHandler::GetCommandManagerForRequest() const
        {
            // The command system and the Animation Editor's widgets are not thread safe.
            if (AZStd::this_thread::get_id() != m_mainThreadId)
            {
                return AZ::Failure(AZStd::string("EMotion FX commands can only be used from the main thread."));
            }

            CommandSystem::CommandManager* commandManager = CommandSystem::GetCommandManager();
            if (!commandManager)
            {
                return AZ::Failure(AZStd::string("The EMotion FX command system is not initialized."));
            }

            return AZ::Success(commandManager);
        }
    } // namespace Integration
} // namespace EMotionFX
