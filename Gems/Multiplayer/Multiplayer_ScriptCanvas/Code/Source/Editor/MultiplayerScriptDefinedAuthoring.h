/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once
#include <AzCore/Outcome/Outcome.h>
#include <AzCore/RTTI/ReflectContext.h>
#include <AzCore/std/string/string.h>

namespace ScriptCanvasMultiplayer
{
    using ScriptDefinedAuthoringResult = AZ::Outcome<AZStd::string, AZStd::string>;
    void ReflectScriptDefinedAuthoring(AZ::ReflectContext* context);
    ScriptDefinedAuthoringResult SaveScriptDefinedSchema(const AZStd::string& path, const AZStd::string& definitionJson, bool overwrite);
    ScriptDefinedAuthoringResult ReadScriptDefinedSchema(const AZStd::string& path);
} // namespace ScriptCanvasMultiplayer
