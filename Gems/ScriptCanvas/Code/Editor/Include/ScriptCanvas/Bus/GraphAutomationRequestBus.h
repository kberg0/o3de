/* Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT */
#pragma once
#include <AzCore/EBus/EBus.h>
#include <AzToolsFramework/Component/EditorComponentAPIBus.h>
#include <AzCore/Outcome/Outcome.h>
#include <AzCore/Math/Vector2.h>
#include <AzCore/std/any.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

namespace ScriptCanvasEditor
{
    //! Native Automation data, marshalled by EditorPythonBindings like other editor maps/any values.
    using AutomationValueMap = AZStd::unordered_map<AZStd::string, AZStd::any>;
    using AutomationValueList = AZStd::vector<AutomationValueMap>;
    using AutomationResult = AZ::Outcome<AutomationValueMap, AZStd::string>;

    //! Dialog-free adapter over the real Script Canvas editor document and GraphCanvas owners.
    //! Graph/node IDs are decimal native ScriptCanvas IDs, not editor-context entity references.
    //! Palette IDs are process-local tree paths plus native factory choices, invalidated when the model changes.
    class GraphAutomationRequests : public AZ::EBusTraits
    {
    public:
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;
        virtual AutomationResult Create(const AZStd::string& path) = 0;
        virtual AutomationResult Open(const AZStd::string& path) = 0;
        virtual AutomationResult Reload(const AZStd::string& graph, bool discard) = 0;
        virtual AutomationResult Describe(const AZStd::string& graph) = 0;
        virtual AutomationResult Palette(const AZStd::string& query, AZ::u32 after, AZ::u32 limit) = 0;
        virtual AutomationResult AddNode(const AZStd::string& graph, const AZStd::string& palette, const AZ::Vector2& position) = 0;
        virtual AutomationResult Connect(const AZStd::string& graph, const AZStd::string& sourceNode,
            const AZStd::string& sourceSlot, const AZStd::string& targetNode, const AZStd::string& targetSlot) = 0;
        virtual AutomationResult SetValue(const AZStd::string& graph, const AZStd::string& node,
            const AZStd::string& slot, const AZStd::any& value) = 0;
        virtual AutomationResult SetVariable(const AZStd::string& graph, const AZStd::string& variable,
            const AZStd::any& value) = 0;
        virtual AutomationResult RemoveNodes(const AZStd::string& graph, const AZStd::vector<AZStd::string>& nodes) = 0;
        virtual AutomationResult RemoveConnections(const AZStd::string& graph, const AZStd::vector<AZStd::string>& connections) = 0;
        //! One active native save. PollSave consumes its completion; CancelSave abandons observation, not disk IO.
        virtual AutomationResult Save(const AZStd::string& graph, const AZStd::string& path) = 0;
        virtual AutomationResult PollSave() = 0;
        virtual void CancelSave() = 0;
        virtual AutomationResult Attach(const AZ::EntityComponentIdPair& component, const AZStd::string& path) = 0;
        virtual AutomationResult Attachment(const AZ::EntityComponentIdPair& component) = 0;
        virtual AutomationResult Undo(const AZStd::string& graph) = 0;
        virtual AutomationResult Redo(const AZStd::string& graph) = 0;
    };
    using GraphAutomationRequestBus = AZ::EBus<GraphAutomationRequests>;

    class GraphAutomationNotifications : public AZ::EBusTraits
    {
    public:
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Multiple;
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;
        virtual void OnAutomationSaveComplete(const AZStd::string& path, const AZStd::string& error) = 0;
    };
    using GraphAutomationNotificationBus = AZ::EBus<GraphAutomationNotifications>;
}
