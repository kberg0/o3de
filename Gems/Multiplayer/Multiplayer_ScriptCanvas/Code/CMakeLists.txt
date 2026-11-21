#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#

ly_add_target(
    NAME ${gem_name}.Static STATIC
    NAMESPACE Gem
    FILES_CMAKE
        scriptcanvas_multiplayer_files.cmake
        scriptcanvas_autogen_files.cmake
    INCLUDE_DIRECTORIES
        PRIVATE
            .
            Source
    BUILD_DEPENDENCIES
        PRIVATE
            Gem::ScriptCanvas.API
        PUBLIC
            Gem::Multiplayer.Common.Static
            Gem::ScriptEvents.Static
    AUTOGEN_RULES
        *.ScriptCanvasNodeable.xml,ScriptCanvasNodeable_Header.jinja,$path/$fileprefix.generated.h
        *.ScriptCanvasNodeable.xml,ScriptCanvasNodeable_Source.jinja,$path/$fileprefix.generated.cpp
)

ly_add_target(
    NAME ${gem_name} ${PAL_TRAIT_MONOLITHIC_DRIVEN_MODULE_TYPE}
    NAMESPACE Gem
    FILES_CMAKE
        scriptcanvas_multiplayer_shared_files.cmake
    INCLUDE_DIRECTORIES
        PRIVATE
            .
            Source
    BUILD_DEPENDENCIES
        PRIVATE
            Legacy::CryCommon
            Gem::${gem_name}.Static
)

# Inject the gem name into the Module source file
ly_add_source_properties(
    SOURCES
        Source/ScriptCanvasMultiplayerModule.cpp
    PROPERTY COMPILE_DEFINITIONS
        VALUES
            O3DE_GEM_NAME=${gem_name}
            O3DE_GEM_VERSION=${gem_version})

# By default, the above module is used by all application types, however, the module depends at runtime to ScriptCanvas
# and the dependency needs to be different per application type
ly_create_alias(NAME ${gem_name}.Unified  NAMESPACE Gem TARGETS Gem::${gem_name} Gem::ScriptCanvas)
ly_create_alias(NAME ${gem_name}.Clients  NAMESPACE Gem TARGETS Gem::${gem_name} Gem::ScriptCanvas)
ly_create_alias(NAME ${gem_name}.Servers  NAMESPACE Gem TARGETS Gem::${gem_name} Gem::ScriptCanvas)
if(PAL_TRAIT_BUILD_HOST_TOOLS)
    ly_add_target(
        NAME ${gem_name}.Editor.Static STATIC
        NAMESPACE Gem
        FILES_CMAKE scriptcanvas_multiplayer_editor_files.cmake
        INCLUDE_DIRECTORIES PUBLIC . Source
        BUILD_DEPENDENCIES PUBLIC
            Gem::${gem_name}.Static
            Gem::Multiplayer.Unified.Static
            Gem::ScriptEvents.Static
            AZ::AzToolsFramework
            AZ::AssetBuilderSDK
    )
    ly_add_target(
        NAME ${gem_name}.Editor ${PAL_TRAIT_MONOLITHIC_DRIVEN_MODULE_TYPE}
        NAMESPACE Gem
        FILES_CMAKE scriptcanvas_multiplayer_editor_shared_files.cmake
        INCLUDE_DIRECTORIES PRIVATE . Source
        BUILD_DEPENDENCIES PRIVATE Gem::${gem_name}.Editor.Static
    )
    ly_create_alias(NAME ${gem_name}.Tools NAMESPACE Gem TARGETS Gem::${gem_name}.Editor Gem::ScriptCanvas.Editor)
    ly_create_alias(NAME ${gem_name}.Builders NAMESPACE Gem TARGETS Gem::${gem_name}.Editor Gem::ScriptCanvas.Editor)
    if(PAL_TRAIT_BUILD_TESTS_SUPPORTED)
        ly_add_target(
            NAME ${gem_name}.Tests MODULE
            NAMESPACE Gem
            FILES_CMAKE scriptcanvas_multiplayer_tests_files.cmake
            INCLUDE_DIRECTORIES PRIVATE . Source ${CMAKE_CURRENT_SOURCE_DIR}/../../../ScriptEvents/Code/Source
            BUILD_DEPENDENCIES PRIVATE AZ::AzTest Gem::${gem_name}.Editor.Static Gem::ScriptCanvas.API
        )
        ly_add_googletest(NAME Gem::${gem_name}.Tests LABELS REQUIRES_tiaf)
    endif()
endif()
