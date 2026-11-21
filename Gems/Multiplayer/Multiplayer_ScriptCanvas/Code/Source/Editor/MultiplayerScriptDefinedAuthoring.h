/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Outcome/Outcome.h>
#include <AzCore/RTTI/ReflectContext.h>
#include <AzCore/std/string/string.h>

namespace ScriptCanvasMultiplayer
{
    //! The saved source path or the JSON text on success; an error message on failure.
    using ScriptDefinedAuthoringResult = AZ::Outcome<AZStd::string, AZStd::string>;

    //! File extension of schema sources, including the dot, as AZ::IO::Path::Extension returns it.
    inline constexpr const char* ScriptDefinedSchemaDotExtension = ".mpschema";

    //! File extension of schema sources without the dot, as asset handlers take it.
    inline constexpr const char* ScriptDefinedSchemaExtension = ScriptDefinedSchemaDotExtension + 1;

    //! Reflects the authoring functions to the behavior context for editor automation.
    void ReflectScriptDefinedAuthoring(AZ::ReflectContext* context);

    //! Validates a definition given as JSON and saves it as a .mpschema source inside the current project.
    //! Saving over an existing source increases its revision. The source is replaced atomically.
    //! @param path Project-relative path of the source.
    //! @param definitionJson The definition in the JSON form JsonSerialization uses.
    //! @param overwrite Allows replacing an existing source.
    ScriptDefinedAuthoringResult SaveScriptDefinedSchema(const AZStd::string& path, const AZStd::string& definitionJson, bool overwrite);

    //! Reads a .mpschema source inside the current project and returns its definition as JSON.
    ScriptDefinedAuthoringResult ReadScriptDefinedSchema(const AZStd::string& path);
} // namespace ScriptCanvasMultiplayer
