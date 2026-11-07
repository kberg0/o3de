/* Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT */
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
#include <AzToolsFramework/ToolsComponents/GenericComponentWrapper.h>

namespace UnitTest
{
    class AutomationPendingComponent final : public AzToolsFramework::Components::EditorComponentBase
    {
    public:
        AZ_EDITOR_COMPONENT(AutomationPendingComponent, "{98A43FE2-13FC-4ED5-96EB-726ACBC79E08}");
        static void Reflect(AZ::ReflectContext* context)
        {
            if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
            { serialize->Class<AutomationPendingComponent, AzToolsFramework::Components::EditorComponentBase>()->Version(0); }
        }
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services)
        { services.push_back(AZ_CRC_CE("AutomationMissingFixtureService")); }
    };

    class PrefabAutomationTests : public PrefabTestFixture
    {
    protected:
        void SetUpEditorFixtureImpl() override
        {
            PrefabTestFixture::SetUpEditorFixtureImpl();
            GetApplication()->RegisterComponentDescriptor(AutomationPendingComponent::CreateDescriptor());
        }
        void TearDownEditorFixtureImpl() override
        {
            if (m_assetSystem)
            {
                m_assetSystem->BusDisconnect();
                m_assetSystem.reset();
                GetApplication()->FindEntity(AZ::SystemEntityId)->FindComponent<AzToolsFramework::AssetSystem::AssetSystemComponent>()->Activate();
            }
            PrefabTestFixture::TearDownEditorFixtureImpl();
        }
        AZStd::unique_ptr<testing::NiceMock<UnitTests::MockAssetSystemRequest>> m_assetSystem;
    };

    TEST_F(PrefabAutomationTests, NativeCreateAndReadOnlyQueriesAreReflected)
    {
        using namespace AzToolsFramework;
        EXPECT_NE(GetApplication()->GetSerializeContext()->FindGenericClassInfo(azrtti_typeid<Prefab::PrefabPathList>()), nullptr);
        auto* context = GetApplication()->GetBehaviorContext();
        const auto bus = context->m_ebuses.find("PrefabPublicRequestBus");
        ASSERT_NE(bus, context->m_ebuses.end());
        for (const auto* name : {"CreateEntity", "SavePrefab", "HasUnsavedChanges", "GetDirtyPrefabPaths", "IsPropagationPending"})
        { EXPECT_NE(bus->second->m_events.find(name), bus->second->m_events.end()) << name; }
        auto root = m_prefabEditorEntityOwnershipInterface->GetRootPrefabInstance();
        ASSERT_TRUE(root);
        Prefab::CreatePrefabResult entity;
        Prefab::PrefabPublicRequestBus::BroadcastResult(entity, &Prefab::PrefabPublicRequests::CreateEntity,
            root->get().GetContainerEntityId(), AZ::Vector3::CreateZero());
        ASSERT_TRUE(entity.IsSuccess()) << entity.GetError().c_str();
        EXPECT_TRUE(entity.GetValue().IsValid());
        Prefab::PrefabPathsResult dirty;
        Prefab::PrefabPublicRequestBus::BroadcastResult(dirty, &Prefab::PrefabPublicRequests::GetDirtyPrefabPaths);
        EXPECT_TRUE(dirty.IsSuccess());
        AZ::Outcome<bool, AZStd::string> pending;
        Prefab::PrefabPublicRequestBus::BroadcastResult(pending, &Prefab::PrefabPublicRequests::IsPropagationPending);
        ASSERT_TRUE(pending.IsSuccess());
        EXPECT_EQ(pending.GetValue(), m_instanceUpdateExecutorInterface->IsUpdatingTemplateInstancesInQueue() ||
            m_instanceUpdateExecutorInterface->HasQueuedTemplateInstances());
    }

    TEST_F(PrefabAutomationTests, SaveAndDirtyQueryUseTheSameTemplateForAbsoluteAndRelativePaths)
    {
        using namespace AzToolsFramework;
        const auto absolute = m_prefabLoaderInterface->GetFullPath("AutomationAssetScan/AutomationSave.prefab");
        ASSERT_TRUE(absolute.IsAbsolute());
        const AZ::IO::Path relative(absolute.Filename());
        const AZ::IO::Path scanFolder(absolute.ParentPath());
        auto* system = GetApplication()->FindEntity(AZ::SystemEntityId)->FindComponent<AssetSystem::AssetSystemComponent>();
        ASSERT_NE(system, nullptr);
        system->Deactivate();
        m_assetSystem = AZStd::make_unique<testing::NiceMock<UnitTests::MockAssetSystemRequest>>();
        ON_CALL(*m_assetSystem, GenerateRelativeSourcePath(testing::_, testing::_, testing::_))
            .WillByDefault([&](const AZStd::string& path, AZStd::string& output, AZStd::string& folder)
            {
                if (AZ::IO::PathView(path) != absolute) { return false; }
                output = relative.Native();
                folder = scanFolder.Native();
                return true;
            });
        bool registered = false;
        ON_CALL(*m_assetSystem, GetSourceInfoBySourcePath(testing::_, testing::_, testing::_))
            .WillByDefault([&](const char* path, AZ::Data::AssetInfo& info, AZStd::string& folder)
            {
                if (!registered || AZ::IO::PathView(path) != relative) { return false; }
                info.m_relativePath = relative.Native();
                folder = scanFolder.Native();
                return true;
            });
        m_assetSystem->BusConnect();
        CreateEditorPrefab(absolute, {CreateEditorEntityUnderRoot("Save path")});
        const auto templateId = m_prefabSystemComponent->GetTemplateIdFromFilePath(relative.c_str());
        ASSERT_NE(templateId, InvalidTemplateId);
        MockPrefabFileIOActionValidator fileIO;
        auto& mock = static_cast<AZ::IO::MockFileIOBase&>(*AZ::IO::FileIOBase::GetInstance());
        ON_CALL(mock, Write(testing::_, testing::_, testing::_, testing::_))
            .WillByDefault([](AZ::IO::HandleType, const void*, AZ::u64 size, AZ::u64* written)
            {
                if (written) { *written = size; }
                return AZ::IO::Result(AZ::IO::ResultCode::Success);
            });
        EXPECT_CALL(mock, Open(testing::StrEq(absolute.c_str()), testing::_, testing::_)).Times(2);
        for (const auto& path : {absolute, relative})
        {
            m_prefabSystemComponent->SetTemplateDirtyFlag(templateId, true);
            AZ::Outcome<bool, AZStd::string> dirty;
            Prefab::PrefabPublicRequestBus::BroadcastResult(dirty, &Prefab::PrefabPublicRequests::HasUnsavedChanges, path.Native());
            ASSERT_TRUE(dirty.IsSuccess());
            EXPECT_TRUE(dirty.GetValue());
            Prefab::PrefabOperationResult saved;
            Prefab::PrefabPublicRequestBus::BroadcastResult(saved, &Prefab::PrefabPublicRequests::SavePrefab, path.Native());
            ASSERT_TRUE(saved.IsSuccess()) << saved.GetError().c_str();
            registered = true;
            Prefab::PrefabPublicRequestBus::BroadcastResult(dirty, &Prefab::PrefabPublicRequests::HasUnsavedChanges, path.Native());
            ASSERT_TRUE(dirty.IsSuccess());
            EXPECT_FALSE(dirty.GetValue());
        }
    }

    TEST_F(PrefabAutomationTests, PropertyTreeUsesTheWrappedRuntimeInstance)
    {
        using namespace AzToolsFramework;
        auto* edit = GetApplication()->GetSerializeContext()->GetEditContext();
        ASSERT_NE(edit, nullptr);
        edit->Class<PrefabNonEditorComponent>("Wrapped runtime", "Wrapped property fixture")
            ->DataElement(AZ::Edit::UIHandlers::Default, &PrefabNonEditorComponent::m_intProperty, "IntProperty", "");
        const auto entityId = CreateEditorEntityUnderRoot("Wrapped properties");
        EditorComponentAPIRequests::AddComponentsOutcome added;
        EditorComponentAPIBus::BroadcastResult(added, &EditorComponentAPIRequests::AddComponentsOfType,
            entityId, AZ::ComponentTypeList{azrtti_typeid<PrefabNonEditorComponent>()});
        ASSERT_TRUE(added.IsSuccess());
        ASSERT_EQ(added.GetValue().size(), 1);
        auto* entity = GetApplication()->FindEntity(entityId);
        ASSERT_NE(entity, nullptr);
        auto* wrapper = azrtti_cast<Components::GenericComponentWrapper*>(entity->FindComponent(added.GetValue().front().GetComponentId()));
        ASSERT_NE(wrapper, nullptr);
        auto* runtime = azrtti_cast<PrefabNonEditorComponent*>(wrapper->GetTemplate());
        ASSERT_NE(runtime, nullptr);
        runtime->m_intProperty = 731;
        EditorComponentAPIRequests::PropertyTreeOutcome tree;
        EditorComponentAPIBus::BroadcastResult(tree, &EditorComponentAPIRequests::BuildComponentPropertyTreeEditor,
            added.GetValue().front());
        ASSERT_TRUE(tree.IsSuccess());
        const auto value = tree.GetValue().GetProperty("IntProperty");
        ASSERT_TRUE(value.IsSuccess());
        EXPECT_EQ(AZStd::any_cast<int>(value.GetValue()), 731);
    }

    TEST_F(PrefabAutomationTests, ComponentEnumerationIncludesPendingAndDisabledInstances)
    {
        using namespace AzToolsFramework;
        const auto entity = CreateEditorEntityUnderRoot("Automation components");
        EditorComponentAPIRequests::GetComponentsOutcome before;
        EditorComponentAPIBus::BroadcastResult(before, &EditorComponentAPIRequests::GetAllComponents, entity);
        ASSERT_TRUE(before.IsSuccess());
        size_t nullTypeCount = 1;
        EditorComponentAPIBus::BroadcastResult(nullTypeCount, &EditorComponentAPIRequests::CountComponentsOfType, entity, AZ::Uuid{});
        EXPECT_EQ(nullTypeCount, 0);
        EditorComponentAPIRequests::AddComponentsOutcome added;
        const AZ::ComponentTypeList types{azrtti_typeid<PrefabTestComponent>(), azrtti_typeid<AutomationPendingComponent>()};
        EditorComponentAPIBus::BroadcastResult(added, &EditorComponentAPIRequests::AddComponentsOfType, entity, types);
        ASSERT_TRUE(added.IsSuccess()) << added.GetError().c_str();
        ASSERT_EQ(added.GetValue().size(), types.size());
        EditorComponentAPIRequests::GetComponentOutcome enabled;
        EditorComponentAPIBus::BroadcastResult(enabled, &EditorComponentAPIRequests::GetComponentOfType,
            entity, azrtti_typeid<PrefabTestComponent>());
        ASSERT_TRUE(enabled.IsSuccess());
        bool disabled = false;
        EditorComponentAPIBus::BroadcastResult(disabled, &EditorComponentAPIRequests::DisableComponents,
            AZStd::vector<AZ::EntityComponentIdPair>{enabled.GetValue()});
        ASSERT_TRUE(disabled);
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
}
