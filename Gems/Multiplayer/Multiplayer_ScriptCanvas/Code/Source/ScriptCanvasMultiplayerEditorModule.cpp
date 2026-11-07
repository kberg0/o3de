/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "Editor/MultiplayerScriptDefinedEditor.h"
#include "MultiplayerScriptDefinedBridge.h"
#include "ScriptCanvasMultiplayerSystemComponent.h"
#include <AzCore/Module/Module.h>

namespace ScriptCanvasMultiplayer
{
    class ScriptCanvasMultiplayerEditorModule : public AZ::Module
    {
    public:
        AZ_RTTI(ScriptCanvasMultiplayerEditorModule, "{73EB2B53-EB5C-4014-A545-218312408FD3}", AZ::Module);
        AZ_CLASS_ALLOCATOR(ScriptCanvasMultiplayerEditorModule, AZ::SystemAllocator);
        ScriptCanvasMultiplayerEditorModule()
        {
            m_descriptors.insert(
                m_descriptors.end(),
                { ScriptCanvasMultiplayerSystemComponent::CreateDescriptor(),
                  MultiplayerScriptDefinedBridge::CreateDescriptor(),
                  EditorMultiplayerScriptDefinedComponent::CreateDescriptor(),
                  MultiplayerScriptDefinedEditorSystemComponent::CreateDescriptor() });
        }
        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return { azrtti_typeid<ScriptCanvasMultiplayerSystemComponent>(),
                     azrtti_typeid<MultiplayerScriptDefinedEditorSystemComponent>() };
        }
    };
} // namespace ScriptCanvasMultiplayer
AZ_DECLARE_MODULE_CLASS(Gem_Multiplayer_ScriptCanvas_Editor, ScriptCanvasMultiplayer::ScriptCanvasMultiplayerEditorModule)
