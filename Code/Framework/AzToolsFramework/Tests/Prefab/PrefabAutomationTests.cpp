/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Prefab/PrefabTestFixture.h>
#include <Prefab/PrefabTestComponent.h>
#include <Prefab/MockPrefabFileIOActionValidator.h>
#include <AssetSystemMocks.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/std/algorithm.h>
#include <AzToolsFramework/Component/EditorComponentAPIBus.h>
#include <AzToolsFramework/Asset/AssetSystemComponent.h>
#include <AzToolsFramework/Entity/PrefabEditorEntityOwnershipInterface.h>
#include <AzToolsFramework/Prefab/PrefabPublicRequestBus.h>
#include <AzToolsFramework/Prefab/PrefabLoaderInterface.h>
#include <AzToolsFramework/Prefab/PrefabPublicNotificationBus.h>
#include <AzToolsFramework/ToolsComponents/GenericComponentWrapper.h>

namespace UnitTest
{
    //! An editor component that requires a service no fixture component provides, so adding it to an entity leaves it pending.
    class AutomationPendingComponent final : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_EDITOR_COMPONENT(AutomationPendingComponent, "{98A43FE2-13FC-4ED5-96EB-726ACBC79E08}");

        static void Reflect(AZ::ReflectContext* context)
        {
            if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
            {
                serialize->Class<AutomationPendingComponent, AzToolsFramework::Components::EditorComponentBase>()->Version(0);
            }
        }

        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services)
        {
            services.push_back(AZ_CRC_CE("AutomationMissingFixtureService"));
        }
    };

    //! Tests the automation APIs on PrefabPublicRequestBus and EditorComponentAPIBus.
    class PrefabAutomationTests : public PrefabTestFixture
    {
    protected:
        // PrefabTestFixture overrides
        void SetUpEditorFixtureImpl() override
        {
            PrefabTestFixture::SetUpEditorFixtureImpl();
            GetApplication()->RegisterComponentDescriptor(AutomationPendingComponent::CreateDescriptor());
        }

        void TearDownEditorFixtureImpl() override
        {
            // A test that replaced the asset system with m_assetSystem gets the real asset system component back.
            if (m_assetSystem)
            {
                m_assetSystem->BusDisconnect();
                m_assetSystem.reset();

                AZ::Entity* systemEntity = GetApplication()->FindEntity(AZ::SystemEntityId);
                systemEntity->FindComponent<AzToolsFramework::AssetSystem::AssetSystemComponent>()->Activate();
            }

            PrefabTestFixture::TearDownEditorFixtureImpl();
        }

        //! Replaces the asset system in tests that need to control path lookups. Torn down with the fixture.
        AZStd::unique_ptr<testing::NiceMock<UnitTests::MockAssetSystemRequest>> m_assetSystem;
    };

    TEST_F(PrefabAutomationTests, NativeCreateAndReadOnlyQueriesAreReflected)
    {
        using namespace AzToolsFramework;

        // The path list type and the new events are reflected for Python.
        const AZ::SerializeContext* serializeContext = GetApplication()->GetSerializeContext();
        EXPECT_NE(serializeContext->FindGenericClassInfo(azrtti_typeid<Prefab::PrefabPathList>()), nullptr);

        auto* context = GetApplication()->GetBehaviorContext();
        const auto bus = context->m_ebuses.find("PrefabPublicRequestBus");
        ASSERT_NE(bus, context->m_ebuses.end());

        const char* const eventNames[] = {
            "CreateEntity", "SavePrefab", "HasUnsavedChanges", "GetDirtyPrefabPaths", "IsPropagationPending"
        };
        for (const char* name : eventNames)
        {
            EXPECT_NE(bus->second->m_events.find(name), bus->second->m_events.end()) << name;
        }

        // CreateEntity creates an entity under the level's root prefab.
        auto root = m_prefabEditorEntityOwnershipInterface->GetRootPrefabInstance();
        ASSERT_TRUE(root);

        Prefab::CreatePrefabResult entity;
        Prefab::PrefabPublicRequestBus::BroadcastResult(
            entity, &Prefab::PrefabPublicRequests::CreateEntity, root->get().GetContainerEntityId(), AZ::Vector3::CreateZero());
        ASSERT_TRUE(entity.IsSuccess()) << entity.GetError().c_str();
        EXPECT_TRUE(entity.GetValue().IsValid());

        // The read-only queries succeed, and IsPropagationPending agrees with the update executor.
        Prefab::PrefabPathsResult dirty;
        Prefab::PrefabPublicRequestBus::BroadcastResult(dirty, &Prefab::PrefabPublicRequests::GetDirtyPrefabPaths);
        EXPECT_TRUE(dirty.IsSuccess());

        AZ::Outcome<bool, AZStd::string> pending;
        Prefab::PrefabPublicRequestBus::BroadcastResult(pending, &Prefab::PrefabPublicRequests::IsPropagationPending);
        ASSERT_TRUE(pending.IsSuccess());

        const bool executorHasWork = m_instanceUpdateExecutorInterface->IsUpdatingTemplateInstancesInQueue() ||
            m_instanceUpdateExecutorInterface->HasQueuedTemplateInstances();
        EXPECT_EQ(pending.GetValue(), executorHasWork);
    }

    TEST_F(PrefabAutomationTests, SaveAndDirtyQueryUseTheSameTemplateForAbsoluteAndRelativePaths)
    {
        using namespace AzToolsFramework;

        const auto absolute = m_prefabLoaderInterface->GetFullPath("AutomationAssetScan/AutomationSave.prefab");
        ASSERT_TRUE(absolute.IsAbsolute());
        const AZ::IO::Path relative(absolute.Filename());
        const AZ::IO::Path scanFolder(absolute.ParentPath());

        // Replace the asset system with a mock that knows only this one prefab.
        auto* system = GetApplication()->FindEntity(AZ::SystemEntityId)->FindComponent<AssetSystem::AssetSystemComponent>();
        ASSERT_NE(system, nullptr);
        system->Deactivate();

        m_assetSystem = AZStd::make_unique<testing::NiceMock<UnitTests::MockAssetSystemRequest>>();
        ON_CALL(*m_assetSystem, GenerateRelativeSourcePath(testing::_, testing::_, testing::_))
            .WillByDefault(
                [&](const AZStd::string& path, AZStd::string& output, AZStd::string& folder)
                {
                    if (AZ::IO::PathView(path) != absolute)
                    {
                        return false;
                    }

                    output = relative.Native();
                    folder = scanFolder.Native();
                    return true;
                });

        // The asset system resolves the template's relative path back to its source file.
        ON_CALL(*m_assetSystem, GetSourceInfoBySourcePath(testing::_, testing::_, testing::_))
            .WillByDefault(
                [&](const char* path, AZ::Data::AssetInfo& info, AZStd::string& folder)
                {
                    if (AZ::IO::PathView(path) != relative)
                    {
                        return false;
                    }

                    info.m_relativePath = relative.Native();
                    folder = scanFolder.Native();
                    return true;
                });
        m_assetSystem->BusConnect();

        CreateEditorPrefab(absolute, { CreateEditorEntityUnderRoot("Save path") });
        const auto templateId = m_prefabSystemComponent->GetTemplateIdFromFilePath(relative.c_str());
        ASSERT_NE(templateId, InvalidTemplateId);

        // Writes succeed without touching the disk, and both saves open the absolute file.
        MockPrefabFileIOActionValidator fileIO;
        auto& mock = static_cast<AZ::IO::MockFileIOBase&>(*AZ::IO::FileIOBase::GetInstance());
        ON_CALL(mock, Write(testing::_, testing::_, testing::_, testing::_))
            .WillByDefault(
                [](AZ::IO::HandleType, const void*, AZ::u64 size, AZ::u64* written)
                {
                    if (written)
                    {
                        *written = size;
                    }

                    return AZ::IO::Result(AZ::IO::ResultCode::Success);
                });
        EXPECT_CALL(mock, Open(testing::StrEq(absolute.c_str()), testing::_, testing::_)).Times(2);

        // Counts the template's save notifications, which both path forms must send.
        struct SaveCounter : Prefab::PrefabTemplateNotificationBus::Handler
        {
            void OnPrefabTemplateSaved() override
            {
                ++m_saves;
            }

            int m_saves = 0;
        };
        SaveCounter saveCounter;
        saveCounter.BusConnect(templateId);

        for (const auto& path : { absolute, relative })
        {
            m_prefabSystemComponent->SetTemplateDirtyFlag(templateId, true);

            AZ::Outcome<bool, AZStd::string> dirty;
            Prefab::PrefabPublicRequestBus::BroadcastResult(dirty, &Prefab::PrefabPublicRequests::HasUnsavedChanges, path.Native());
            ASSERT_TRUE(dirty.IsSuccess());
            EXPECT_TRUE(dirty.GetValue());

            Prefab::PrefabOperationResult saved;
            Prefab::PrefabPublicRequestBus::BroadcastResult(saved, &Prefab::PrefabPublicRequests::SavePrefab, path.Native());
            ASSERT_TRUE(saved.IsSuccess()) << saved.GetError().c_str();

            Prefab::PrefabPublicRequestBus::BroadcastResult(dirty, &Prefab::PrefabPublicRequests::HasUnsavedChanges, path.Native());
            ASSERT_TRUE(dirty.IsSuccess());
            EXPECT_FALSE(dirty.GetValue());
        }

        EXPECT_EQ(saveCounter.m_saves, 2);
        saveCounter.BusDisconnect();
    }

    TEST_F(PrefabAutomationTests, PropertyTreeUsesTheWrappedRuntimeInstance)
    {
        using namespace AzToolsFramework;

        // A value the property can only hold if it is read from the wrapped runtime component.
        static constexpr int WrappedPropertyValue = 731;

        auto* edit = GetApplication()->GetSerializeContext()->GetEditContext();
        ASSERT_NE(edit, nullptr);
        edit->Class<PrefabNonEditorComponent>("Wrapped runtime", "Wrapped property fixture")
            ->DataElement(AZ::Edit::UIHandlers::Default, &PrefabNonEditorComponent::m_intProperty, "IntProperty", "");

        // Adding a runtime component to an editor entity wraps it in a GenericComponentWrapper.
        const auto entityId = CreateEditorEntityUnderRoot("Wrapped properties");
        EditorComponentAPIRequests::AddComponentsOutcome added;
        EditorComponentAPIBus::BroadcastResult(
            added,
            &EditorComponentAPIRequests::AddComponentsOfType,
            entityId,
            AZ::ComponentTypeList{ azrtti_typeid<PrefabNonEditorComponent>() });
        ASSERT_TRUE(added.IsSuccess());
        ASSERT_EQ(added.GetValue().size(), 1);

        auto* entity = GetApplication()->FindEntity(entityId);
        ASSERT_NE(entity, nullptr);
        AZ::Component* component = entity->FindComponent(added.GetValue().front().GetComponentId());
        auto* wrapper = azrtti_cast<Components::GenericComponentWrapper*>(component);
        ASSERT_NE(wrapper, nullptr);
        auto* runtime = azrtti_cast<PrefabNonEditorComponent*>(wrapper->GetTemplate());
        ASSERT_NE(runtime, nullptr);
        runtime->m_intProperty = WrappedPropertyValue;

        // The property tree reads the runtime component's value through the wrapper.
        EditorComponentAPIRequests::PropertyTreeOutcome tree;
        EditorComponentAPIBus::BroadcastResult(
            tree, &EditorComponentAPIRequests::BuildComponentPropertyTreeEditor, added.GetValue().front());
        ASSERT_TRUE(tree.IsSuccess());

        const auto value = tree.GetValue().GetProperty("IntProperty");
        ASSERT_TRUE(value.IsSuccess());
        EXPECT_EQ(AZStd::any_cast<int>(value.GetValue()), WrappedPropertyValue);
    }

    TEST_F(PrefabAutomationTests, ComponentEnumerationIncludesPendingAndDisabledInstances)
    {
        using namespace AzToolsFramework;

        const auto entity = CreateEditorEntityUnderRoot("Automation components");
        EditorComponentAPIRequests::GetComponentsOutcome before;
        EditorComponentAPIBus::BroadcastResult(before, &EditorComponentAPIRequests::GetAllComponents, entity);
        ASSERT_TRUE(before.IsSuccess());

        // A null type matches no component. Start from a nonzero count to prove the call wrote it.
        size_t nullTypeCount = 1;
        EditorComponentAPIBus::BroadcastResult(nullTypeCount, &EditorComponentAPIRequests::CountComponentsOfType, entity, AZ::Uuid{});
        EXPECT_EQ(nullTypeCount, 0);

        // Add one component that becomes active and one that stays pending.
        EditorComponentAPIRequests::AddComponentsOutcome added;
        const AZ::ComponentTypeList types{ azrtti_typeid<PrefabTestComponent>(), azrtti_typeid<AutomationPendingComponent>() };
        EditorComponentAPIBus::BroadcastResult(added, &EditorComponentAPIRequests::AddComponentsOfType, entity, types);
        ASSERT_TRUE(added.IsSuccess()) << added.GetError().c_str();
        ASSERT_EQ(added.GetValue().size(), types.size());

        // Disable the active one.
        EditorComponentAPIRequests::GetComponentOutcome enabled;
        EditorComponentAPIBus::BroadcastResult(
            enabled, &EditorComponentAPIRequests::GetComponentOfType, entity, azrtti_typeid<PrefabTestComponent>());
        ASSERT_TRUE(enabled.IsSuccess());

        bool disabled = false;
        EditorComponentAPIBus::BroadcastResult(
            disabled, &EditorComponentAPIRequests::DisableComponents, AZStd::vector<AZ::EntityComponentIdPair>{ enabled.GetValue() });
        ASSERT_TRUE(disabled);

        // GetAllComponents lists both the disabled and the pending component, and neither is enabled.
        EditorComponentAPIRequests::GetComponentsOutcome all;
        EditorComponentAPIBus::BroadcastResult(all, &EditorComponentAPIRequests::GetAllComponents, entity);
        ASSERT_TRUE(all.IsSuccess());
        EXPECT_EQ(all.GetValue().size(), before.GetValue().size() + types.size());

        for (const auto& component : added.GetValue())
        {
            EXPECT_NE(AZStd::find(all.GetValue().begin(), all.GetValue().end(), component), all.GetValue().end());

            bool active = true;
            EditorComponentAPIBus::BroadcastResult(active, &EditorComponentAPIRequests::IsComponentEnabled, component);
            EXPECT_FALSE(active);
        }
    }
} // namespace UnitTest
