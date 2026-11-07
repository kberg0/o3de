/* Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT */
#include "GraphAutomation.h"
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/string/conversions.h>
#include <AzCore/std/utility/charconv.h>
#include <Editor/View/Windows/MainWindow.h>
#include <Editor/Undo/ScriptCanvasUndoManager.h>
#include <Editor/View/Widgets/NodePalette/CreateNodeMimeEvent.h>
#include <GraphCanvas/Components/SceneBus.h>
#include <GraphCanvas/Components/Nodes/NodeBus.h>
#include <GraphCanvas/Widgets/GraphCanvasTreeItem.h>
#include <GraphCanvas/Widgets/GraphCanvasMimeEvent.h>
#include <GraphCanvas/Widgets/NodePalette/TreeItems/NodePaletteTreeItem.h>
#include <ScriptCanvas/Bus/EditorScriptCanvasBus.h>
#include <ScriptCanvas/Core/ConnectionBus.h>
#include <ScriptCanvas/Core/GraphBus.h>
#include <ScriptCanvas/Core/Node.h>
#include <ScriptCanvas/Core/ModifiableDatumView.h>
#include <ScriptCanvas/Data/Data.h>
#include <ScriptCanvas/GraphCanvas/MappingBus.h>
#include <ScriptCanvas/Variable/VariableBus.h>

namespace ScriptCanvasEditor
{
    class GraphAutomation::GraphEdit
    {
    public:
        GraphEdit(MainWindow& owner, AZStd::string_view label)
            : m_owner(owner), m_editorBatch(AZStd::string(label).c_str()), m_graphBatch(label)
        { m_owner.PushPreventUndoStateUpdate(); }
        ~GraphEdit()
        {
            m_owner.PopPreventUndoStateUpdate();
            m_owner.PostUndoPoint(m_owner.GetActiveScriptCanvasId());
            m_owner.SignalSceneDirty(m_owner.m_activeGraph);
        }
    private:
        MainWindow& m_owner;
        AzToolsFramework::ScopedUndoBatch m_editorBatch;
        ScriptCanvasEditor::ScopedUndoBatch m_graphBatch;
    };

    AutomationValueMap GraphAutomation::DescribeNode(ScriptCanvas::Node& node, AZ::EntityId graph)
    {
        AutomationValueList slotDescriptions;
        for (const auto& slot : node.GetSlots())
        {
            const auto endpoint = ScriptCanvas::Endpoint(node.GetEntityId(), slot.GetId());
            GraphCanvas::Endpoint canvasEndpoint;
            EditorGraphRequestBus::EventResult(canvasEndpoint, graph,
                &EditorGraphRequests::ConvertToGraphCanvasEndpoint, endpoint);
            AutomationValueMap entry{{"id", slot.GetId().ToString()}, {"name", slot.GetName()},
                {"direction", AZStd::string(slot.IsInput() ? "in" : "out")},
                {"kind", AZStd::string(slot.IsExecution() ? "execution" : "data")},
                {"graphCanvasNode", IdText(canvasEndpoint.GetNodeId())},
                {"graphCanvasSlot", IdText(canvasEndpoint.GetSlotId())},
                {"type", ScriptCanvas::Data::ToAZType(slot.GetDataType()).ToString<AZStd::string>()},
                {"typeName", ScriptCanvas::Data::GetName(slot.GetDataType())}};
            if (const auto* datum = node.FindDatum(slot.GetId()))
            { entry.emplace("value", datum->ToAny()); }
            slotDescriptions.push_back(AZStd::move(entry));
        }
        AZ::EntityId canvas;
        SceneMemberMappingRequestBus::EventResult(canvas, node.GetEntityId(), &SceneMemberMappingRequests::GetGraphCanvasEntityId);
        return {{"id", IdText(node.GetEntityId())}, {"graphCanvas", IdText(canvas)},
            {"name", node.GetNodeName()}, {"type", node.RTTI_GetType().ToString<AZStd::string>()}, {"slots", AZStd::move(slotDescriptions)}};
    }

    AutomationResult GraphAutomation::Describe(const AZStd::string& graph)
    {
        auto window = GraphWindow(graph);
        if (!window) { return AZ::Failure(window.GetError()); }
        const auto graphId = EntityId(graph);
        AZStd::vector<AZ::EntityId> nodeIds, connectionIds;
        ScriptCanvas::GraphRequestBus::EventResult(nodeIds, graphId, &ScriptCanvas::GraphRequests::GetNodes);
        ScriptCanvas::GraphRequestBus::EventResult(connectionIds, graphId, &ScriptCanvas::GraphRequests::GetConnections);
        if (nodeIds.size() > 4096 || connectionIds.size() > 8192)
        { return AZ::Failure(AZStd::string("Graph exceeds the bounded automation description limit")); }
        AutomationValueList nodes, connections, variables;
        for (const auto id : nodeIds)
        {
            ScriptCanvas::Node* node = nullptr;
            ScriptCanvas::GraphRequestBus::EventResult(node, graphId, &ScriptCanvas::GraphRequests::FindNode, id);
            if (!node) { return AZ::Failure(AZStd::string("Native graph contains an unavailable node")); }
            nodes.push_back(DescribeNode(*node, graphId));
        }
        for (const auto id : connectionIds)
        {
            ScriptCanvas::Endpoint source, target;
            ScriptCanvas::ConnectionRequestBus::EventResult(source, id, &ScriptCanvas::ConnectionRequests::GetSourceEndpoint);
            ScriptCanvas::ConnectionRequestBus::EventResult(target, id, &ScriptCanvas::ConnectionRequests::GetTargetEndpoint);
            AZ::EntityId canvas;
            SceneMemberMappingRequestBus::EventResult(canvas, id, &SceneMemberMappingRequests::GetGraphCanvasEntityId);
            connections.push_back({{"id", IdText(id)}, {"graphCanvas", IdText(canvas)},
                {"sourceNode", IdText(source.GetNodeId())}, {"sourceSlot", source.GetSlotId().ToString()},
                {"targetNode", IdText(target.GetNodeId())}, {"targetSlot", target.GetSlotId().ToString()}});
        }
        const ScriptCanvas::GraphVariableMapping* mapping = nullptr;
        ScriptCanvas::GraphRequestBus::EventResult(mapping, graphId, &ScriptCanvas::GraphRequests::GetVariables);
        if (mapping)
        {
            for (const auto& [id, variable] : *mapping)
            {
                variables.push_back({{"id", id.ToString()}, {"name", AZStd::string(variable.GetVariableName())},
                    {"type", ScriptCanvas::Data::ToAZType(variable.GetDataType()).ToString<AZStd::string>()},
                    {"value", variable.GetDatum()->ToAny()}});
            }
        }
        auto result = Identity(*window.GetValue());
        result.emplace("nodes", AZStd::move(nodes));
        result.emplace("connections", AZStd::move(connections));
        result.emplace("variables", AZStd::move(variables));
        return AZ::Success(AZStd::move(result));
    }

    GraphAutomation::PaletteChoices GraphAutomation::ExpandPaletteChoices(
        AZStd::unique_ptr<GraphCanvas::GraphCanvasMimeEvent> event)
    {
        PaletteChoices choices;
        if (const auto* multi = azrtti_cast<const MultiCreateNodeMimeEvent*>(event.get()))
        {
            // These are the existing non-interactive send/receive/get/set alternatives.
            for (auto* choice : multi->CreateMimeEvents()) { choices.emplace_back(choice); }
        }
        else if (event) { choices.push_back(AZStd::move(event)); }
        return choices;
    }

    AutomationResult GraphAutomation::Palette(const AZStd::string& query, AZ::u32 after, AZ::u32 limit)
    {
        auto window = Window();
        if (!window) { return AZ::Failure(window.GetError()); }
        if (limit == 0 || limit > 256) { return AZ::Failure(AZStd::string("Palette page limit must be 1..256")); }
        const auto* root = window.GetValue()->GetNodePaletteRoot();
        if (!root) { return AZ::Failure(AZStd::string("Native node palette is unavailable")); }
        AutomationValueList entries;
        AZ::u32 matched = 0;
        AZStd::string needle(query);
        AZStd::to_lower(needle.begin(), needle.end());
        AZStd::function<void(const GraphCanvas::GraphCanvasTreeItem*, const AZStd::string&, const AZStd::string&)> visit;
        visit = [&](const auto* item, const AZStd::string& key, const AZStd::string& parent)
        {
            const auto* palette = azrtti_cast<const GraphCanvas::NodePaletteTreeItem*>(item);
            const AZStd::string name = palette ? AZStd::string(palette->GetName().toUtf8().constData()) : AZStd::string();
            auto path = parent + "/" + name;
            auto lowered = path;
            AZStd::to_lower(lowered.begin(), lowered.end());
            if (lowered.find(needle) != AZStd::string::npos)
            {
                auto choices = ExpandPaletteChoices(AZStd::unique_ptr<GraphCanvas::GraphCanvasMimeEvent>(item->CreateMimeEvent()));
                if (!palette || palette->IsEnabled())
                {
                    for (size_t index = 0; index < choices.size(); ++index)
                    {
                        if (choices[index] && matched++ >= after && entries.size() < limit)
                        { entries.push_back({{"id", key + "#" + AZStd::to_string(index)}, {"name", name}, {"path", path},
                            {"factory", AZStd::string(choices[index]->RTTI_GetTypeName())}}); }
                    }
                }
            }
            for (int index = 0; index < item->GetChildCount(); ++index)
            { visit(item->FindChildByRow(index), key + "/" + AZStd::to_string(index), path); }
        };
        visit(root, "", "");
        const auto next = after >= matched ? matched : after + static_cast<AZ::u32>(entries.size());
        return AZ::Success(AutomationValueMap{{"entries", AZStd::move(entries)}, {"total", matched},
            {"next", next}});
    }

    AutomationResult GraphAutomation::AddNode(const AZStd::string& graph, const AZStd::string& key, const AZ::Vector2& position)
    {
        auto window = GraphWindow(graph);
        if (!window) { return AZ::Failure(window.GetError()); }
        if (!position.IsFinite()) { return AZ::Failure(AZStd::string("Node position must be finite")); }
        auto& owner = *window.GetValue();
        const auto separator = key.find('#');
        size_t choiceIndex = 0;
        if (separator == AZStd::string::npos)
        { return AZ::Failure(AZStd::string("Use a complete palette id including its factory choice")); }
        const auto choiceParsed = AZStd::from_chars(key.data() + separator + 1, key.data() + key.size(), choiceIndex);
        if (choiceParsed.ec != AZStd::errc{} || choiceParsed.ptr != key.data() + key.size())
        { return AZ::Failure(AZStd::string("Invalid palette factory choice")); }
        const auto treePath = AZStd::string_view(key).substr(0, separator);
        const auto* item = owner.GetNodePaletteRoot();
        size_t cursor = 0;
        while (item && cursor < treePath.size())
        {
            if (treePath[cursor++] != '/') { return AZ::Failure(AZStd::string("Invalid palette tree path")); }
            auto end = treePath.find('/', cursor);
            if (end == AZStd::string_view::npos) { end = treePath.size(); }
            int index = -1;
            const auto parsed = AZStd::from_chars(treePath.data() + cursor, treePath.data() + end, index);
            if (parsed.ec != AZStd::errc{} || parsed.ptr != treePath.data() + end || index < 0 || index >= item->GetChildCount())
            { return AZ::Failure(AZStd::string("Palette identifier is invalid or stale")); }
            item = item->FindChildByRow(index);
            cursor = end;
        }
        auto choices = ExpandPaletteChoices(AZStd::unique_ptr<GraphCanvas::GraphCanvasMimeEvent>(item ? item->CreateMimeEvent() : nullptr));
        const auto* palette = azrtti_cast<const GraphCanvas::NodePaletteTreeItem*>(item);
        if (choiceIndex >= choices.size() || !choices[choiceIndex] || (palette && !palette->IsEnabled()))
        { return AZ::Failure(AZStd::string("Palette entry has no enabled native node factory")); }
        GraphEdit edit(owner, "ScriptCanvas automation: add node");
        owner.SignalAutomationBegin();
        const auto pair = owner.ProcessCreateNodeMimeEvent(choices[choiceIndex].get(), owner.GetActiveGraphCanvasGraphId(), position);
        owner.SignalAutomationEnd();
        auto nativeId = pair.m_scriptCanvasId;
        if (!nativeId.IsValid())
        {
            // Handler factories return a visual event child; its wrapper owns the native node.
            AZ::EntityId wrapper;
            GraphCanvas::NodeRequestBus::EventResult(wrapper, pair.m_graphCanvasId, &GraphCanvas::NodeRequests::GetWrappingNode);
            AZStd::any* data = nullptr;
            GraphCanvas::NodeRequestBus::EventResult(data, wrapper.IsValid() ? wrapper : pair.m_graphCanvasId,
                &GraphCanvas::NodeRequests::GetUserData);
            if (const auto* id = AZStd::any_cast<AZ::EntityId>(data)) { nativeId = *id; }
        }
        ScriptCanvas::Node* node = nullptr;
        ScriptCanvas::GraphRequestBus::EventResult(node, EntityId(graph), &ScriptCanvas::GraphRequests::FindNode, nativeId);
        if (!node || !pair.m_graphCanvasId.IsValid())
        { return AZ::Failure(AZStd::string("Native node factory failed; inspect the graph for partial changes")); }
        auto description = DescribeNode(*node, EntityId(graph));
        description.emplace("createdGraphCanvas", IdText(pair.m_graphCanvasId));
        return AZ::Success(AZStd::move(description));
    }

    AutomationResult GraphAutomation::Connect(const AZStd::string& graph, const AZStd::string& sourceNode,
        const AZStd::string& sourceSlot, const AZStd::string& targetNode, const AZStd::string& targetSlot)
    {
        auto window = GraphWindow(graph);
        if (!window) { return AZ::Failure(window.GetError()); }
        const auto graphId = EntityId(graph);
        const ScriptCanvas::Endpoint source(EntityId(sourceNode), ScriptCanvas::SlotId(AZ::Uuid::CreateString(sourceSlot.c_str())));
        const ScriptCanvas::Endpoint target(EntityId(targetNode), ScriptCanvas::SlotId(AZ::Uuid::CreateString(targetSlot.c_str())));
        AZ::Outcome<void, AZStd::string> valid = AZ::Failure(AZStd::string("Graph has no connection validator"));
        ScriptCanvas::GraphRequestBus::EventResult(valid, graphId, &ScriptCanvas::GraphRequests::CanCreateConnectionBetween, source, target);
        if (!valid) { return AZ::Failure(valid.GetError()); }
        GraphCanvas::Endpoint canvasSource, canvasTarget;
        EditorGraphRequestBus::EventResult(canvasSource, graphId, &EditorGraphRequests::ConvertToGraphCanvasEndpoint, source);
        EditorGraphRequestBus::EventResult(canvasTarget, graphId, &EditorGraphRequests::ConvertToGraphCanvasEndpoint, target);
        if (!canvasSource.IsValid() || !canvasTarget.IsValid())
        { return AZ::Failure(AZStd::string("Native endpoint conversion refused a slot")); }
        GraphEdit edit(*window.GetValue(), "ScriptCanvas automation: connect");
        AZ::EntityId canvasConnection;
        GraphCanvas::SceneRequestBus::EventResult(canvasConnection, window.GetValue()->GetActiveGraphCanvasGraphId(),
            &GraphCanvas::SceneRequests::CreateConnectionBetween, canvasSource, canvasTarget);
        if (!canvasConnection.IsValid()) { return AZ::Failure(AZStd::string("Native GraphCanvas connection creation failed")); }
        return AZ::Success(AutomationValueMap{{"graphCanvas", IdText(canvasConnection)}});
    }

    AZStd::any GraphAutomation::CopyDatumValue(const AZStd::any& value)
    {
        // The native Python any marshaller lends strings; ScriptCanvas datums must own them.
        if (const auto* text = AZStd::any_cast<AZStd::string_view>(&value)) { return AZStd::any(AZStd::string(*text)); }
        return value;
    }

    AutomationResult GraphAutomation::AssignValue(ScriptCanvas::ModifiableDatumView& view, const AZStd::any& value)
    {
        if (!view.IsValid() || value.empty()) { return AZ::Failure(AZStd::string("Datum is not writable or value is empty")); }
        const auto type = ScriptCanvas::Data::FromAZType(value.type());
        if (value.type() != ScriptCanvas::Data::ToAZType(view.GetDataType())) { return AZ::Failure(AZStd::string("Value must have the datum's native type; no lossy coercion")); }
        ScriptCanvas::Datum replacement(type, ScriptCanvas::Datum::eOriginality::Copy, AZStd::any_cast<void>(&value), value.type());
        view.AssignToDatum(AZStd::move(replacement));
        return AZ::Success(AutomationValueMap{{"value", view.GetDatum()->ToAny()}});
    }

    AutomationResult GraphAutomation::SetValue(const AZStd::string& graph, const AZStd::string& nodeId,
        const AZStd::string& slotId, const AZStd::any& value)
    {
        auto window = GraphWindow(graph);
        if (!window) { return AZ::Failure(window.GetError()); }
        ScriptCanvas::Node* node = nullptr;
        ScriptCanvas::GraphRequestBus::EventResult(node, EntityId(graph), &ScriptCanvas::GraphRequests::FindNode, EntityId(nodeId));
        const ScriptCanvas::SlotId id(AZ::Uuid::CreateString(slotId.c_str()));
        const auto* slot = node ? node->GetSlot(id) : nullptr;
        if (!slot || !slot->IsData() || !slot->IsInput() || slot->IsVariableReference())
        { return AZ::Failure(AZStd::string("Expected an unreferenced data input slot in this graph")); }
        bool connected = false;
        ScriptCanvas::GraphRequestBus::EventResult(connected, EntityId(graph), &ScriptCanvas::GraphRequests::IsEndpointConnected, slot->GetEndpoint());
        if (connected) { return AZ::Failure(AZStd::string("Disconnect the data input before setting its literal value")); }
        const auto nativeValue = CopyDatumValue(value);
        ScriptCanvas::ModifiableDatumView view;
        if (!node->FindModifiableDatumView(id, view) || nativeValue.type() != ScriptCanvas::Data::ToAZType(view.GetDataType()))
        { return AZ::Failure(AZStd::string("Slot has no matching writable native datum")); }
        GraphEdit edit(*window.GetValue(), "ScriptCanvas automation: set value");
        return AssignValue(view, nativeValue);
    }

    AutomationResult GraphAutomation::SetVariable(const AZStd::string& graph, const AZStd::string& variableId, const AZStd::any& value)
    {
        auto window = GraphWindow(graph);
        if (!window) { return AZ::Failure(window.GetError()); }
        ScriptCanvas::GraphVariable* variable = nullptr;
        ScriptCanvas::GraphVariableManagerRequestBus::EventResult(variable, EntityId(graph),
            &ScriptCanvas::GraphVariableManagerRequests::FindVariableById, ScriptCanvas::VariableId(AZ::Uuid::CreateString(variableId.c_str())));
        const auto nativeValue = CopyDatumValue(value);
        if (!variable || nativeValue.type() != ScriptCanvas::Data::ToAZType(variable->GetDataType()))
        { return AZ::Failure(AZStd::string("Variable is absent or value has a different native type")); }
        ScriptCanvas::ModifiableDatumView view;
        variable->ConfigureDatumView(view);
        GraphEdit edit(*window.GetValue(), "ScriptCanvas automation: set variable");
        return AssignValue(view, nativeValue);
    }

    AutomationResult GraphAutomation::RemoveNodes(const AZStd::string& graph, const AZStd::vector<AZStd::string>& nodes)
    {
        auto window = GraphWindow(graph);
        if (!window) { return AZ::Failure(window.GetError()); }
        AZStd::vector<AZ::EntityId> canvasIds;
        for (const auto& text : nodes)
        {
            ScriptCanvas::Node* node = nullptr;
            ScriptCanvas::GraphRequestBus::EventResult(node, EntityId(graph), &ScriptCanvas::GraphRequests::FindNode, EntityId(text));
            if (!node) { return AZ::Failure(AZStd::string("Node is absent from this graph")); }
            AZ::EntityId canvas;
            SceneMemberMappingRequestBus::EventResult(canvas, node->GetEntityId(), &SceneMemberMappingRequests::GetGraphCanvasEntityId);
            if (!canvas.IsValid()) { return AZ::Failure(AZStd::string("Node has no GraphCanvas mapping")); }
            canvasIds.push_back(canvas);
        }
        if (canvasIds.empty()) { return AZ::Failure(AZStd::string("Provide at least one node")); }
        GraphEdit edit(*window.GetValue(), "ScriptCanvas automation: remove nodes");
        window.GetValue()->DeleteNodes(window.GetValue()->GetActiveGraphCanvasGraphId(), canvasIds);
        return AZ::Success(AutomationValueMap{{"removed", static_cast<AZ::u32>(canvasIds.size())}});
    }

    AutomationResult GraphAutomation::RemoveConnections(const AZStd::string& graph, const AZStd::vector<AZStd::string>& connections)
    {
        auto window = GraphWindow(graph);
        if (!window) { return AZ::Failure(window.GetError()); }
        AZStd::vector<AZ::EntityId> owned, canvasIds;
        ScriptCanvas::GraphRequestBus::EventResult(owned, EntityId(graph), &ScriptCanvas::GraphRequests::GetConnections);
        for (const auto& text : connections)
        {
            const auto id = EntityId(text);
            if (AZStd::find(owned.begin(), owned.end(), id) == owned.end())
            { return AZ::Failure(AZStd::string("Connection is absent from this graph")); }
            AZ::EntityId canvas;
            SceneMemberMappingRequestBus::EventResult(canvas, id, &SceneMemberMappingRequests::GetGraphCanvasEntityId);
            if (!canvas.IsValid()) { return AZ::Failure(AZStd::string("Connection has no GraphCanvas mapping")); }
            canvasIds.push_back(canvas);
        }
        if (canvasIds.empty()) { return AZ::Failure(AZStd::string("Provide at least one connection")); }
        GraphEdit edit(*window.GetValue(), "ScriptCanvas automation: remove connections");
        window.GetValue()->DeleteConnections(window.GetValue()->GetActiveGraphCanvasGraphId(), canvasIds);
        return AZ::Success(AutomationValueMap{{"removed", static_cast<AZ::u32>(canvasIds.size())}});
    }
}
