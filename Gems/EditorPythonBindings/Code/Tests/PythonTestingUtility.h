/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Component/ComponentApplication.h>
#include <stdexcept>
#include <AzCore/Memory/AllocationRecords.h>
#include <AzCore/Settings/SettingsRegistryMergeUtils.h>
#include <AzCore/Settings/CommandLine.h>
#include <AzFramework/IO/LocalFileIO.h>
#include <AzFramework/Application/Application.h>
#include <AzFramework/CommandLine/CommandRegistrationBus.h>

#include <AzQtComponents/Utilities/QtPluginPaths.h>

#include <AzTest/AzTest.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzFramework/API/ApplicationAPI.h>
#include <AzToolsFramework/API/EditorPythonConsoleBus.h>

#include <Source/PythonSystemComponent.h>
#include <Source/PythonReflectionComponent.h>
#include <Source/PythonMarshalComponent.h>

namespace UnitTest
{
    struct CommandRegistrationBusSupression
        : public AzFramework::CommandRegistrationBus::Handler
    {
        CommandRegistrationBusSupression()
        {
            BusConnect();
        }

        ~CommandRegistrationBusSupression()
        {
            BusDisconnect();
        }

        bool RegisterCommand(AZStd::string_view, AZStd::string_view, AZ::u32, AzFramework::CommandFunction) override
        {
            return true;
        }

        bool UnregisterCommand(AZStd::string_view) override
        {
            return true;
        }
    };

    struct PythonTestingFixture
        : public ::testing::Test
        , protected AzFramework::ApplicationRequests::Bus::Handler
    {
        class FileIOHelper
        {
        public:
            AZ::IO::LocalFileIO m_fileIO;
            AZ::IO::FileIOBase* m_prevFileIO;

            FileIOHelper()
            {
                m_prevFileIO = AZ::IO::FileIOBase::GetInstance();
                AZ::IO::FileIOBase::SetInstance(&m_fileIO);
            }

            ~FileIOHelper()
            {
                AZ::IO::FileIOBase::SetInstance(m_prevFileIO);
            }
        };

        void SetUp() override
        {
            // Tests can run from a relocated binary snapshot; Python belongs to the configured source engine.
            m_engineRoot = AZ::IO::FixedMaxPath(EDITOR_PYTHON_BINDINGS_TEST_ENGINE_ROOT).LexicallyNormal();

            m_fileIOHelper = AZStd::make_unique<FileIOHelper>();
            m_fileIOHelper->m_fileIO.SetAlias("@engroot@", m_engineRoot.c_str());

            AzFramework::Application::Descriptor appDesc;
            appDesc.m_recordingMode = AZ::Debug::AllocationRecords::Mode::RECORD_NO_RECORDS;
            AZ::ComponentApplication::StartupParameters startupParameters;
            startupParameters.m_loadSettingsRegistry = false;
            m_app.Create(appDesc, startupParameters);
            AZ::CommandLine sourceEngine;
            sourceEngine.Parse(AZ::CommandLine::ParamContainer{
                AZStd::string::format("--engine-path=%s", m_engineRoot.c_str()) });
            AZ::SettingsRegistryMergeUtils::StoreCommandLineToRegistry(*AZ::SettingsRegistry::Get(), sourceEngine);
            AZ::SettingsRegistry::Get()->Set(AZ::SettingsRegistryMergeUtils::FilePathKey_EngineRootFolder, m_engineRoot.Native());

            AzFramework::ApplicationRequests::Bus::Handler::BusConnect();
        }

        void TearDown() override
        {
            AzFramework::ApplicationRequests::Bus::Handler::BusDisconnect();

            m_commandRegistrationBusSupression.reset();
            m_fileIOHelper.reset();
            m_app.Destroy();
        }

        void SimulateEditorBecomingInitialized(bool useCommandRegistrationBusSupression = true)
        {
            if (useCommandRegistrationBusSupression)
            {
                m_commandRegistrationBusSupression = AZStd::make_unique<CommandRegistrationBusSupression>();
            }

            auto editorPythonEventsInterface = AZ::Interface<AzToolsFramework::EditorPythonEventsInterface>::Get();
            if (!editorPythonEventsInterface)
            {
                throw std::runtime_error("Embedded Python service is missing from the test fixture");
            }
            if (!editorPythonEventsInterface->IsPythonActive() && !editorPythonEventsInterface->StartPython())
            {
                throw std::runtime_error("Embedded Python failed to initialize; inspect its startup diagnostics");
            }
        }

        void RegisterComponentDescriptors()
        {
            m_app.RegisterComponentDescriptor(EditorPythonBindings::PythonSystemComponent::CreateDescriptor());
            m_app.RegisterComponentDescriptor(EditorPythonBindings::PythonReflectionComponent::CreateDescriptor());
            m_app.RegisterComponentDescriptor(EditorPythonBindings::PythonMarshalComponent::CreateDescriptor());
        }

        void Activate(AZ::Entity& e)
        {
            e.CreateComponent<EditorPythonBindings::PythonSystemComponent>();
            e.CreateComponent<EditorPythonBindings::PythonReflectionComponent>();
            e.CreateComponent<EditorPythonBindings::PythonMarshalComponent>();
            e.Init();
            e.Activate();
        }

        //////////////////////////////////////////////////////////////////////////
        // AzFramework::ApplicationRequests::Bus::Handler
        // required pure virtual overrides
        void NormalizePath(AZStd::string& ) override {}
        void NormalizePathKeepCase(AZStd::string& ) override {}
        void CalculateBranchTokenForEngineRoot(AZStd::string& ) const override {}

        AZ::ComponentApplication m_app;
        AZStd::unique_ptr<FileIOHelper> m_fileIOHelper;
        AZStd::unique_ptr<CommandRegistrationBusSupression> m_commandRegistrationBusSupression;
        AZ::IO::FixedMaxPath m_engineRoot;
    };
}
