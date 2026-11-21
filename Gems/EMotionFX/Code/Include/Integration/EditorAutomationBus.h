/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/EBus/EBus.h>
#include <AzCore/Outcome/Outcome.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

namespace EMotionFX
{
    namespace Integration
    {
        //! Editor automation access to the EMotion FX command system: the undoable text commands that the Animation Editor
        //! uses to author actors, motions, motion sets and anim graphs, for example "CreateMotionSet -name Locomotion".
        //! Python reaches it as azlmbr.emotionfx.editor.EMotionFXEditorAutomationRequestBus.
        //! Requests are only accepted on the main thread; from any other thread they fail with an error.
        class EditorAutomationRequests : public AZ::EBusTraits
        {
        public:
            static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
            static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

            //! The result text of a command on success, or the reason it failed.
            using CommandOutcome = AZ::Outcome<AZStd::string, AZStd::string>;

            //! The command names on success, or the reason they could not be listed.
            using CommandNamesOutcome = AZ::Outcome<AZStd::vector<AZStd::string>, AZStd::string>;

            virtual ~EditorAutomationRequests() = default;

            //! Executes one command string and records it in the command history, so Undo() can revert it.
            virtual CommandOutcome ExecuteCommand(const AZStd::string& command) = 0;

            //! Returns the names of all registered commands, sorted.
            virtual CommandNamesOutcome GetCommandNames() = 0;

            //! Returns a command's description followed by one line per parameter (name, type, required or default
            //! value, description), or an error when no command has that name.
            virtual CommandOutcome GetCommandSyntax(const AZStd::string& commandName) = 0;

            //! Undoes the most recent command in the history, or fails when there is nothing to undo.
            virtual CommandOutcome Undo() = 0;

            //! Redoes the most recently undone command, or fails when there is nothing to redo.
            virtual CommandOutcome Redo() = 0;
        };

        using EditorAutomationRequestBus = AZ::EBus<EditorAutomationRequests>;
    } // namespace Integration
} // namespace EMotionFX
