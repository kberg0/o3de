/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/std/parallel/thread.h>
#include <Integration/EditorAutomationBus.h>

namespace AZ
{
    class ReflectContext;
}

namespace CommandSystem
{
    class CommandManager;
}

namespace EMotionFX
{
    namespace Integration
    {
        //! Serves EditorAutomationRequestBus with the command manager returned by CommandSystem::GetCommandManager().
        //! That is the single command manager EMotion Studio creates when the Editor registers its views, so commands work
        //! whether or not the Animation Editor window is open, and they share its undo history.
        //! Commands also run in game mode: the command system already refuses to remove or change objects that the
        //! runtime owns, the same protection that makes the Animation Editor usable in game mode.
        class EditorAutomationHandler : private EditorAutomationRequestBus::Handler
        {
        public:
            //! Longest command string, or command name, that requests accept. Commands can carry serialized object
            //! contents, so the limit is generous; it only keeps external input bounded.
            static constexpr size_t MaxCommandLength = 256 * 1024;

            //! Reflects EditorAutomationRequestBus to the behavior context with Automation scope, so only Editor
            //! automation (EditorPythonBindings) sees it.
            static void Reflect(AZ::ReflectContext* context);

            //! Connects to the bus. The calling thread becomes the main thread, the only one requests are accepted from.
            void Activate();

            //! Disconnects from the bus.
            void Deactivate();

        private:
            // EditorAutomationRequestBus::Handler overrides
            CommandOutcome ExecuteCommand(const AZStd::string& command) override;
            CommandNamesOutcome GetCommandNames() override;
            CommandOutcome GetCommandSyntax(const AZStd::string& commandName) override;
            CommandOutcome Undo() override;
            CommandOutcome Redo() override;

            //! Returns the command manager when the current request may use it, or why it may not.
            AZ::Outcome<CommandSystem::CommandManager*, AZStd::string> GetCommandManagerForRequest() const;

            AZStd::thread_id m_mainThreadId;
        };
    } // namespace Integration
} // namespace EMotionFX
