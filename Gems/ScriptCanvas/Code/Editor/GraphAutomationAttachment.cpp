/* Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT */
#include "GraphAutomation.h"
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <AzToolsFramework/Entity/EditorEntityContextBus.h>
#include <ScriptCanvas/Components/EditorScriptCanvasComponent.h>
#include <ScriptCanvas/Components/EditorUtils.h>

namespace ScriptCanvasEditor
{
    AZ::Outcome<EditorScriptCanvasComponent*, AZStd::string> GraphAutomation::ScriptComponent(const AZ::EntityComponentIdPair& pair)
    {
        bool editorEntity = false;
        AzToolsFramework::EditorEntityContextRequestBus::BroadcastResult(editorEntity,
            &AzToolsFramework::EditorEntityContextRequests::IsEditorEntity, pair.GetEntityId());
        AZ::Entity* entity = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, pair.GetEntityId());
        auto* component = editorEntity && entity ? azrtti_cast<EditorScriptCanvasComponent*>(entity->FindComponent(pair.GetComponentId())) : nullptr;
        if (!component) { return AZ::Failure(AZStd::string("Expected an attached, enabled ScriptCanvas editor component in this context")); }
        return AZ::Success(component);
    }

    AutomationResult GraphAutomation::Attach(const AZ::EntityComponentIdPair& pair, const AZStd::string& text)
    {
        auto window = Window();
        if (!window) { return AZ::Failure(window.GetError()); }
        auto component = ScriptComponent(pair);
        if (!component) { return AZ::Failure(component.GetError()); }
        auto path = SourcePath(text);
        if (!path) { return AZ::Failure(path.GetError()); }
        auto source = CreateFromAnyPath(SourceHandle(), path.GetValue());
        if (!source || !source->IsDescriptionValid())
        { return AZ::Failure(AZStd::string("Asset Processor has not registered this ScriptCanvas source")); }
        AzToolsFramework::ScopedUndoBatch undo("ScriptCanvas automation: attach source");
        // Use the same primary-source owner as the editor asset picker, not a serialized-memory write.
        component.GetValue()->SetPrimaryAsset(AZ::Data::AssetId(source->Id(), 0));
        AzToolsFramework::PropertyEditorEntityChangeNotificationBus::Event(pair.GetEntityId(),
            &AzToolsFramework::PropertyEditorEntityChangeNotifications::OnEntityComponentPropertyChanged, pair.GetComponentId());
        undo.MarkEntityDirty(pair.GetEntityId());
        return Attachment(pair);
    }

    AutomationResult GraphAutomation::Attachment(const AZ::EntityComponentIdPair& pair)
    {
        auto window = Window();
        if (!window) { return AZ::Failure(window.GetError()); }
        auto component = ScriptComponent(pair);
        if (!component) { return AZ::Failure(component.GetError()); }
        const auto& source = component.GetValue()->GetSource();
        return AZ::Success(AutomationValueMap{{"selected", source.IsDescriptionValid()},
            {"sourceUuid", source.Id().ToString<AZStd::string>()}, {"path", source.AbsolutePath().Native()}});
    }
}
