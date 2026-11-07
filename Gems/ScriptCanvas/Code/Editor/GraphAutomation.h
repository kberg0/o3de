/* Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT */
#pragma once
#include <ScriptCanvas/Bus/GraphAutomationRequestBus.h>
#include <AzCore/std/optional.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <ScriptCanvas/Core/Core.h>

namespace GraphCanvas { class GraphCanvasTreeItem; class GraphCanvasMimeEvent; }
namespace ScriptCanvas { class Node; class ModifiableDatumView; }
namespace ScriptCanvasEditor
{
    class MainWindow;
    class EditorScriptCanvasComponent;
    //! Owned by the existing ScriptCanvas editor SystemComponent. No runtime/builders handler.
    class GraphAutomation final
        : private GraphAutomationRequestBus::Handler
        , private GraphAutomationNotificationBus::Handler
    {
    public:
        static void Reflect(AZ::ReflectContext* context);
        void Activate();
        void Deactivate();
    private:
        friend class GraphAutomationTests;
        class GraphEdit;
        using WindowResult = AZ::Outcome<MainWindow*, AZStd::string>;
        WindowResult Window();
        WindowResult GraphWindow(const AZStd::string& graph);
        static AZ::Outcome<AZ::IO::Path, AZStd::string> SourcePath(const AZStd::string& path);
        static AZStd::string IdText(AZ::EntityId id);
        static AZ::EntityId EntityId(const AZStd::string& text);
        static AutomationValueMap Identity(MainWindow& window);
        static AutomationValueMap DescribeNode(ScriptCanvas::Node& node, AZ::EntityId graph);
        using PaletteChoices = AZStd::vector<AZStd::unique_ptr<GraphCanvas::GraphCanvasMimeEvent>>;
        static PaletteChoices ExpandPaletteChoices(AZStd::unique_ptr<GraphCanvas::GraphCanvasMimeEvent> event);
        static AZ::Outcome<EditorScriptCanvasComponent*, AZStd::string> ScriptComponent(const AZ::EntityComponentIdPair& component);
        static AZStd::any CopyDatumValue(const AZStd::any& value);
        static AutomationResult AssignValue(ScriptCanvas::ModifiableDatumView& view, const AZStd::any& value);
        void OnAutomationSaveComplete(const AZStd::string& path, const AZStd::string& error) override;
        AutomationResult Create(const AZStd::string& path) override;
        AutomationResult Open(const AZStd::string& path) override;
        AutomationResult Reload(const AZStd::string& graph, bool discard) override;
        AutomationResult Describe(const AZStd::string& graph) override;
        AutomationResult Palette(const AZStd::string& query, AZ::u32 after, AZ::u32 limit) override;
        AutomationResult AddNode(const AZStd::string& graph, const AZStd::string& palette, const AZ::Vector2& position) override;
        AutomationResult Connect(const AZStd::string& graph, const AZStd::string& sourceNode,
            const AZStd::string& sourceSlot, const AZStd::string& targetNode, const AZStd::string& targetSlot) override;
        AutomationResult SetValue(const AZStd::string& graph, const AZStd::string& node,
            const AZStd::string& slot, const AZStd::any& value) override;
        AutomationResult SetVariable(const AZStd::string& graph, const AZStd::string& variable, const AZStd::any& value) override;
        AutomationResult RemoveNodes(const AZStd::string& graph, const AZStd::vector<AZStd::string>& nodes) override;
        AutomationResult RemoveConnections(const AZStd::string& graph, const AZStd::vector<AZStd::string>& connections) override;
        AutomationResult Save(const AZStd::string& graph, const AZStd::string& path) override;
        AutomationResult PollSave() override;
        void CancelSave() override;
        AutomationResult Attach(const AZ::EntityComponentIdPair& component, const AZStd::string& path) override;
        AutomationResult Attachment(const AZ::EntityComponentIdPair& component) override;
        AutomationResult Undo(const AZStd::string& graph) override;
        AutomationResult Redo(const AZStd::string& graph) override;
        bool m_saving = false;
        AZStd::optional<AutomationResult> m_saveResult; // Only the currently observed save, consumed by PollSave.
    };
}
