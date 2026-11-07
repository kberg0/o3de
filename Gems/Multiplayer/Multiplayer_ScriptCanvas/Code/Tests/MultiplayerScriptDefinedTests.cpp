/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Component/ComponentApplication.h>
#include <AzCore/IO/ByteContainerStream.h>
#include <AzCore/IO/SystemFile.h>
#include <AzCore/JSON/document.h>
#include <AzCore/Math/MathReflection.h>
#include <AzCore/Serialization/Json/JsonSystemComponent.h>
#include <AzCore/Serialization/Json/RegistrationContext.h>
#include <AzCore/Serialization/Utils.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/Settings/SettingsRegistryMergeUtils.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzFramework/IO/LocalFileIO.h>
#include <AzTest/AzTest.h>
#include <Editor/MultiplayerScriptDefinedAuthoring.h>
#include <Editor/MultiplayerScriptDefinedEditor.h>
#include <Multiplayer/Components/MultiplayerScriptDefinedComponent.h>
#include <Multiplayer/Components/MultiplayerScriptDefinedInputComponent.h>
#include <MultiplayerScriptDefinedBridge.h>
#include <MultiplayerScriptDefinedInterface.h>
#include <ScriptCanvas/Libraries/Core/Method.h>
#include <ScriptEvents/ScriptEventsBus.h>
#include <ScriptEventsSystemComponent.h>
#include <limits>

namespace ScriptCanvasMultiplayer
{
    using namespace Multiplayer;
    class ScriptDefinedEditorTests : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_app = AZStd::make_unique<AZ::ComponentApplication>();
            AZ::ComponentApplication::Descriptor descriptor;
            descriptor.m_useExistingAllocator = true;
            AZ::ComponentApplication::StartupParameters startup;
            startup.m_loadDynamicModules = false;
            startup.m_loadStaticModules = false;
            startup.m_loadSettingsRegistry = false;
            startup.m_createEditContext = true;
            m_app->Create(descriptor, startup);
            AZ::Data::AssetData::Reflect(m_app->GetSerializeContext());
            AZ::MathReflect(m_app->GetBehaviorContext());
            ReflectScriptDefinedIntegers(m_app->GetSerializeContext());
            ReflectScriptDefinedIntegers(m_app->GetBehaviorContext());
            ScriptEvents::ScriptEventsSystemComponent::Reflect(m_app->GetSerializeContext());
            ScriptEvents::ScriptEventsSystemComponent::Reflect(m_app->GetBehaviorContext());
            MultiplayerComponent::Reflect(m_app->GetSerializeContext());
            MultiplayerScriptDefinedComponent::Reflect(m_app->GetSerializeContext());
            MultiplayerScriptDefinedInputComponent::Reflect(m_app->GetSerializeContext());
            AzToolsFramework::Components::EditorComponentBase::Reflect(m_app->GetSerializeContext());
            EditorMultiplayerScriptDefinedComponent::Reflect(m_app->GetSerializeContext());
            MultiplayerScriptDefinedBridge::Reflect(m_app->GetSerializeContext());
            AZ::Data::AssetManager::Create({});
            m_scriptSystem = AZStd::make_unique<ScriptEvents::ScriptEventsSystemComponentRuntimeImpl>();
            m_scriptSystem->RegisterAssetHandler();
            m_fileIO = AZStd::make_unique<AZ::IO::LocalFileIO>();
            AZ::IO::FileIOBase::SetInstance(m_fileIO.get());
            m_schemaHandler = AZStd::make_unique<MultiplayerScriptDefinedAssetHandler>();
            m_schemaHandler->Register();
            m_definition.m_name = "TestActor";
            ScriptDefinedField health;
            health.m_name = "Health";
            health.m_type = ScriptDefinedRpcType::UInt16;
            health.m_defaultValue = "100";
            m_definition.m_properties.push_back(health);
            ScriptDefinedField jump;
            jump.m_name = "Jump";
            jump.m_type = ScriptDefinedRpcType::Boolean;
            m_definition.m_inputs.push_back(jump);
            ScriptDefinedField team;
            team.m_name = "Team";
            team.m_type = ScriptDefinedRpcType::UInt8;
            team.m_defaultValue = "2";
            m_definition.m_archetypes.push_back(team);
            ScriptDefinedRpcDefinition rpc;
            rpc.m_name = "Hit";
            rpc.m_delivery = RpcDeliveryType::AutonomousToAuthority;
            ScriptDefinedParameter damage;
            damage.m_name = "Damage";
            damage.m_type = ScriptDefinedRpcType::UInt16;
            ScriptDefinedParameter origin;
            origin.m_name = "Origin";
            origin.m_type = ScriptDefinedRpcType::Vector3;
            rpc.m_parameters = { damage, origin };
            m_definition.m_rpcs.push_back(rpc);
        }
        void TearDown() override
        {
            m_definition = {};
            m_scriptSystem->CleanUp();
            m_scriptSystem->UnregisterAssetHandler();
            m_scriptSystem.reset();
            m_schemaHandler->Unregister();
            m_schemaHandler.reset();
            AZ::Data::AssetManager::Destroy();
            AZ::IO::FileIOBase::SetInstance(nullptr);
            m_fileIO.reset();
            m_app->Destroy();
            m_app.reset();
        }
        AZStd::unique_ptr<AZ::ComponentApplication> m_app;
        AZStd::unique_ptr<ScriptEvents::ScriptEventsSystemComponentRuntimeImpl> m_scriptSystem;
        AZStd::unique_ptr<AZ::IO::LocalFileIO> m_fileIO;
        AZStd::unique_ptr<MultiplayerScriptDefinedAssetHandler> m_schemaHandler;
        MultiplayerScriptDefinedDefinition m_definition;
    };

    TEST_F(ScriptDefinedEditorTests, GeneratedInterfaceContainsOnlyDeclaredTypedMembersAndKeepsIdsOnRename)
    {
        auto first = MakeScriptDefinedInterface(m_definition);
        ASSERT_TRUE(first.Validate());
        EXPECT_TRUE(first.IsGenerated());
        ASSERT_EQ(first.GetMethods().size(), 10);
        ScriptEvents::Method send;
        ASSERT_TRUE(first.FindMethod("Send_Hit", send));
        ASSERT_EQ(send.GetParameters().size(), 2);
        EXPECT_EQ(send.GetParameters()[0].GetType(), azrtti_typeid<double>());
        EXPECT_EQ(send.GetParameters()[1].GetType(), azrtti_typeid<AZ::Vector3>());
        const auto methodId = send.GetEventId();
        const auto parameterId = send.GetParameters()[0].GetNameProperty().GetId();
        m_definition.m_rpcs[0].m_name = "DamageActor";
        m_definition.m_rpcs[0].m_parameters[0].m_name = "Amount";
        auto renamed = MakeScriptDefinedInterface(m_definition);
        ASSERT_TRUE(renamed.FindMethod("Send_DamageActor", send));
        EXPECT_EQ(send.GetEventId(), methodId);
        EXPECT_EQ(send.GetParameters()[0].GetNameProperty().GetId(), parameterId);
        m_definition.m_rpcs.clear();
        m_definition.m_inputs.clear();
        auto smaller = MakeScriptDefinedInterface(m_definition);
        EXPECT_FALSE(smaller.FindMethod("Send_Hit", send));
        EXPECT_FALSE(smaller.FindMethod("OnCreateInput", send));
    }

    TEST_F(ScriptDefinedEditorTests, NumericConversionsRejectOverflowFractionalIntegersAndPreserveExact64Bits)
    {
        double number = 65535;
        AZ::BehaviorArgument argument;
        argument.Set(&number);
        ScriptDefinedRpcValue value;
        EXPECT_TRUE(ReadScriptDefinedArgument(ScriptDefinedRpcType::UInt16, argument, value));
        EXPECT_EQ(AZStd::get<uint16_t>(value), 65535);
        number = 65536;
        argument.Set(&number);
        EXPECT_FALSE(ReadScriptDefinedArgument(ScriptDefinedRpcType::UInt16, argument, value));
        number = 1.25;
        argument.Set(&number);
        EXPECT_FALSE(ReadScriptDefinedArgument(ScriptDefinedRpcType::Int8, argument, value));
        const auto exact = ScriptDefinedUInt64::FromDecimal("18446744073709551615");
        ASSERT_TRUE(exact.IsValid());
        EXPECT_EQ(exact.ToDecimal(), "18446744073709551615");
        auto copy = exact;
        argument.Set(&copy);
        ASSERT_TRUE(ReadScriptDefinedArgument(ScriptDefinedRpcType::UInt64, argument, value));
        EXPECT_EQ(AZStd::get<uint64_t>(value), std::numeric_limits<uint64_t>::max());
        EXPECT_FALSE(exact.Add(ScriptDefinedUInt64::FromDecimal("1")).IsValid());
        EXPECT_FALSE(ScriptDefinedInt64::FromDecimal("9223372036854775808").IsValid());
        EXPECT_TRUE(ScriptDefinedInt64::FromDecimal("-9223372036854775808").IsValid());
    }

    TEST_F(ScriptDefinedEditorTests, AssetEditorSavesIncreaseInterfaceRevisionAndPreserveMemberIds)
    {
        AZ::Data::Asset<MultiplayerScriptDefinedAsset> schema(
            aznew MultiplayerScriptDefinedAsset(AZ::Data::AssetId(AZ::Uuid::CreateRandom()), AZ::Data::AssetData::AssetStatus::Ready),
            AZ::Data::AssetLoadBehavior::PreLoad);
        schema->m_definition = m_definition;
        for (AZ::u32 revision = 2; revision <= 3; ++revision)
        {
            AZStd::vector<AZ::u8> bytes;
            AZ::IO::ByteContainerStream stream(&bytes);
            ASSERT_TRUE(m_schemaHandler->SaveAssetData(schema, &stream));
            EXPECT_EQ(schema->m_definition.m_revision, revision);
            stream.Seek(0, AZ::IO::GenericStream::ST_SEEK_BEGIN);
            AZStd::unique_ptr<MultiplayerScriptDefinedAsset> saved(AZ::Utils::LoadObjectFromStream<MultiplayerScriptDefinedAsset>(stream));
            ASSERT_NE(saved, nullptr);
            EXPECT_EQ(saved->m_definition.m_revision, revision);
            EXPECT_EQ(saved->m_definition.m_properties[0].m_id, m_definition.m_properties[0].m_id);
        }
    }

    TEST_F(ScriptDefinedEditorTests, EditorExportsNativeStateAndAddsInputCompanionOnlyWhenDeclared)
    {
        AZ::Data::Asset<MultiplayerScriptDefinedAsset> schema(
            aznew MultiplayerScriptDefinedAsset(AZ::Data::AssetId(AZ::Uuid::CreateRandom()), AZ::Data::AssetData::AssetStatus::Ready),
            AZ::Data::AssetLoadBehavior::PreLoad);
        schema->m_definition = m_definition;
        EditorMultiplayerScriptDefinedComponent editor;
        editor.SetDefinitionAsset(schema);
        AZ::Entity game;
        editor.BuildGameEntity(&game);
        auto* state = game.FindComponent<MultiplayerScriptDefinedComponent>();
        ASSERT_NE(state, nullptr);
        ASSERT_NE(state->GetDefinition(), nullptr);
        EXPECT_EQ(state->GetDefinition()->m_id, schema.GetId().m_guid);
        ScriptDefinedRpcValue health;
        ASSERT_TRUE(state->ReadProperty(m_definition.m_properties[0].m_id, health));
        EXPECT_EQ(AZStd::get<uint16_t>(health), 100);
        EXPECT_NE(game.FindComponent<MultiplayerScriptDefinedInputComponent>(), nullptr);
        EXPECT_NE(game.FindComponent<MultiplayerScriptDefinedBridge>(), nullptr);
        schema->m_definition.m_inputs.clear();
        AZ::Entity noInput;
        editor.BuildGameEntity(&noInput);
        EXPECT_EQ(noInput.FindComponent<MultiplayerScriptDefinedInputComponent>(), nullptr);
    }

    TEST_F(ScriptDefinedEditorTests, BuilderWritesSchemaAndScriptEventsProductsFromAnEditorAsset)
    {
        MultiplayerScriptDefinedAsset schema;
        schema.m_definition = m_definition;
        const AZ::IO::Path folder =
            AZ::IO::Path(m_app->GetExecutableFolder()) / AZ::Uuid::CreateRandom().ToFixedString(false, false).c_str();
        ASSERT_TRUE(AZ::IO::SystemFile::CreateDir(folder.c_str()));
        const AZ::IO::Path source = folder / "actor.mpschema";
        ASSERT_TRUE(AZ::Utils::SaveObjectToFile(source.Native(), AZ::ObjectStream::ST_XML, &schema));
        MultiplayerScriptDefinedBuilder builder;
        AssetBuilderSDK::ProcessJobRequest request;
        request.m_fullPath = source.Native();
        request.m_sourceFile = "actor.mpschema";
        request.m_tempDirPath = folder.Native();
        request.m_sourceFileUUID = AZ::Uuid::CreateRandom();
        AssetBuilderSDK::ProcessJobResponse response;
        builder.ProcessJob(request, response);
        ASSERT_EQ(response.m_resultCode, AssetBuilderSDK::ProcessJobResult_Success);
        ASSERT_EQ(response.m_outputProducts.size(), 2);
        EXPECT_EQ(response.m_outputProducts[1].m_productSubID, ScriptDefinedInterfaceSubId);
        AZStd::unique_ptr<MultiplayerScriptDefinedAsset> compiled(
            AZ::Utils::LoadObjectFromFile<MultiplayerScriptDefinedAsset>(response.m_outputProducts[0].m_productFileName));
        ASSERT_NE(compiled, nullptr);
        EXPECT_EQ(compiled->m_definition.m_id, request.m_sourceFileUUID);
        AZStd::unique_ptr<ScriptEvents::ScriptEventsAsset> product(
            AZ::Utils::LoadObjectFromFile<ScriptEvents::ScriptEventsAsset>(response.m_outputProducts[1].m_productFileName));
        ASSERT_NE(product, nullptr);
        EXPECT_TRUE(product->m_definition.IsGenerated());
        EXPECT_EQ(product->m_definition.GetVersion(), MakeScriptDefinedInterface(m_definition).GetVersion());
        EXPECT_EQ(product->m_definition.GetName(), MakeScriptDefinedInterface(compiled->m_definition).GetName());
        EXPECT_TRUE(product->m_definition.Validate());
        product.reset();
        for (const auto& output : response.m_outputProducts)
        {
            AZ::IO::SystemFile::Delete(output.m_productFileName.c_str());
        }
        AZ::IO::SystemFile::Delete(source.c_str());
        AZ::IO::SystemFile::DeleteDir(folder.c_str());
    }

    TEST_F(ScriptDefinedEditorTests, EveryNativeValueTypeProducesScriptCanvasExposablePins)
    {
        m_definition.m_properties.clear();
        m_definition.m_inputs.clear();
        m_definition.m_archetypes.clear();
        m_definition.m_rpcs.clear();
        for (unsigned type = 0; type <= static_cast<unsigned>(ScriptDefinedRpcType::Entity); ++type)
        {
            ScriptDefinedField field;
            field.m_name = AZStd::string::format("Value%u", type);
            field.m_type = static_cast<ScriptDefinedRpcType>(type);
            m_definition.m_properties.push_back(AZStd::move(field));
        }
        const AZ::Data::AssetId assetId(AZ::Uuid::CreateRandom(), ScriptDefinedInterfaceSubId);
        AZ::Data::Asset<ScriptEvents::ScriptEventsAsset> asset(
            aznew ScriptEvents::ScriptEventsAsset(assetId, AZ::Data::AssetData::AssetStatus::Ready), AZ::Data::AssetLoadBehavior::PreLoad);
        asset->m_definition = MakeScriptDefinedInterface(m_definition);
        ASSERT_TRUE(asset->m_definition.Validate());
        AZStd::intrusive_ptr<ScriptEvents::Internal::ScriptEventRegistration> registration;
        ScriptEvents::ScriptEventBus::BroadcastResult(
            registration, &ScriptEvents::ScriptEventRequests::RegisterScriptEvent, assetId, asset->m_definition.GetVersion());
        ASSERT_NE(registration, nullptr);
        registration->CompleteRegistration(asset);
        auto* bus = registration->GetBehaviorBus(asset->m_definition.GetVersion());
        ASSERT_NE(bus, nullptr);
        ASSERT_EQ(bus->m_events.size(), 48);
        for (const auto& event : bus->m_events)
        {
            const auto exposed = ScriptCanvas::IsExposable(*event.second.m_event);
            EXPECT_TRUE(exposed.IsSuccess()) << event.first.c_str();
        }
    }

    TEST_F(ScriptDefinedEditorTests, AutomationSavesEditableSchemasWithStableIdsAndMonotonicRevisions)
    {
        auto* jsonContext = m_app->GetJsonRegistrationContext();
        if (!jsonContext->GetSerializerForType(azrtti_typeid<AZStd::string>()))
        {
            m_app->RegisterComponentDescriptor(AZ::JsonSystemComponent::CreateDescriptor());
        }
        const AZ::IO::Path folder =
            AZ::IO::Path(m_app->GetExecutableFolder()) / AZ::Uuid::CreateRandom().ToFixedString(false, false).c_str();
        auto* registry = AZ::SettingsRegistry::Get();
        ASSERT_NE(registry, nullptr);
        registry->Set(AZ::SettingsRegistryMergeUtils::FilePathKey_ProjectPath, folder.Native());
        const AZStd::string source = "Assets/actor.mpschema";
        const AZStd::string definition = R"({"Name":"AgentAvatar","Properties":[{"Name":"Health","Type":7,"Default":"100"}]})";
        const auto created = SaveScriptDefinedSchema(source, definition, false);
        ASSERT_TRUE(created) << created.GetError().c_str();
        auto first = ReadScriptDefinedSchema(source);
        ASSERT_TRUE(first) << first.GetError().c_str();
        EXPECT_FALSE(SaveScriptDefinedSchema(source, definition, false));
        EXPECT_FALSE(SaveScriptDefinedSchema("../escape.mpschema", definition, false));
        EXPECT_FALSE(SaveScriptDefinedSchema("Assets/invalid.mpschema", R"({"Name":"Invalid Name"})", false));
        const auto invalidJsonType = SaveScriptDefinedSchema(source, R"({"Name":"AgentAvatar","Properties":true})", true);
        ASSERT_FALSE(invalidJsonType);
        EXPECT_NE(invalidJsonType.GetError().find("Invalid serialized definition JSON:"), AZStd::string::npos);
        ASSERT_TRUE(SaveScriptDefinedSchema(source, first.GetValue(), true));
        auto second = ReadScriptDefinedSchema(source);
        ASSERT_TRUE(second) << second.GetError().c_str();
        rapidjson::Document before, after;
        before.Parse(first.GetValue().c_str());
        after.Parse(second.GetValue().c_str());
        EXPECT_EQ(after["Revision"].GetUint(), before["Revision"].GetUint() + 1);
        EXPECT_STREQ(after["Properties"][0]["Id"].GetString(), before["Properties"][0]["Id"].GetString());
        EXPECT_STREQ(after["Properties"][0]["Default"].GetString(), "100");
        AZ::IO::SystemFile::Delete((folder / source).c_str());
        AZ::IO::SystemFile::DeleteDir((folder / "Assets").c_str());
        AZ::IO::SystemFile::DeleteDir(folder.c_str());
    }

    class FakeScriptDefinedRequests : public ScriptDefinedRequestBus::Handler
    {
    public:
        MultiplayerScriptDefinedDefinition m_definition;
        ScriptDefinedRpcValue m_value = uint16_t{ 100 };
        AZStd::fixed_vector<ScriptDefinedRpcValue, ScriptDefinedArgumentCapacity> m_arguments;
        const MultiplayerScriptDefinedDefinition* GetDefinition() const override
        {
            return &m_definition;
        }
        bool ReadProperty(const AZ::Uuid&, ScriptDefinedRpcValue& value) const override
        {
            value = m_value;
            return true;
        }
        bool WriteProperty(const AZ::Uuid&, const ScriptDefinedRpcValue& value) override
        {
            m_value = value;
            return true;
        }
        bool ReadArchetype(const AZ::Uuid&, ScriptDefinedRpcValue& value) const override
        {
            value = uint8_t{ 2 };
            return true;
        }
        bool InvokeRpc(const AZ::Uuid&, const AZStd::fixed_vector<ScriptDefinedRpcValue, ScriptDefinedArgumentCapacity>& values) override
        {
            m_arguments = values;
            return true;
        }
    };

    TEST_F(ScriptDefinedEditorTests, ScriptEventMethodsActuallyInvokeRuntimeRequestsAndReturnTypedResults)
    {
        FakeScriptDefinedRequests requests;
        requests.m_definition = m_definition;
        AZ::Entity entity;
        requests.BusConnect(entity.GetId());
        auto* bridge = aznew MultiplayerScriptDefinedBridge();
        ASSERT_TRUE(entity.AddComponent(bridge));
        const auto assetId = AZ::Data::AssetId(AZ::Uuid::CreateRandom(), ScriptDefinedInterfaceSubId);
        AZ::Data::Asset<ScriptEvents::ScriptEventsAsset> asset(
            aznew ScriptEvents::ScriptEventsAsset(assetId, AZ::Data::AssetData::AssetStatus::Ready), AZ::Data::AssetLoadBehavior::PreLoad);
        asset->m_definition = MakeScriptDefinedInterface(m_definition);
        bridge->Configure(asset);
        bridge->Init();
        bridge->Activate();
        AZStd::intrusive_ptr<ScriptEvents::Internal::ScriptEventRegistration> registration;
        ScriptEvents::ScriptEventBus::BroadcastResult(
            registration, &ScriptEvents::ScriptEventRequests::GetScriptEvent, assetId, asset->m_definition.GetVersion());
        ASSERT_NE(registration, nullptr);
        auto* bus = registration->GetBehaviorBus(asset->m_definition.GetVersion());
        ASSERT_NE(bus, nullptr);
        for (const auto& event : bus->m_events)
        {
            const auto exposed = ScriptCanvas::IsExposable(*event.second.m_event);
            EXPECT_TRUE(exposed.IsSuccess()) << event.first.c_str();
        }
        AZ::EntityId address = entity.GetId();
        double value = 123;
        bool success = false;
        AZ::BehaviorArgument arguments[2];
        arguments[0].Set(&address);
        arguments[1].Set(&value);
        AZ::BehaviorArgument result;
        result.Set(&success);
        ASSERT_TRUE(bus->m_events.at("Set_Health").m_event->Call(arguments, 2, &result));
        EXPECT_TRUE(success);
        EXPECT_EQ(AZStd::get<uint16_t>(requests.m_value), 123);
        double health = 0;
        result.Set(&health);
        ASSERT_TRUE(bus->m_events.at("Get_Health").m_event->Call(arguments, 1, &result));
        EXPECT_EQ(health, 123);
        value = -1;
        success = true;
        arguments[1].Set(&value);
        result.Set(&success);
        ASSERT_TRUE(bus->m_events.at("Set_Health").m_event->Call(arguments, 2, &result));
        EXPECT_FALSE(success);
        EXPECT_EQ(AZStd::get<uint16_t>(requests.m_value), 123);

        value = 8;
        AZ::Vector3 origin(1, 2, 3);
        AZ::BehaviorArgument rpcArguments[3];
        rpcArguments[0].Set(&address);
        rpcArguments[1].Set(&value);
        rpcArguments[2].Set(&origin);
        success = false;
        result.Set(&success);
        ASSERT_TRUE(bus->m_events.at("Send_Hit").m_event->Call(rpcArguments, 3, &result));
        EXPECT_TRUE(success);
        ASSERT_EQ(requests.m_arguments.size(), 2);
        EXPECT_EQ(AZStd::get<uint16_t>(requests.m_arguments[0]), 8);
        EXPECT_EQ(AZStd::get<AZ::Vector3>(requests.m_arguments[1]), origin);

        struct Received
        {
            unsigned m_calls = 0;
            double m_value = 0;
        } received;
        AZ::BehaviorEBusHandler* receiver = nullptr;
        ASSERT_TRUE(bus->m_createHandler->InvokeResult(receiver));
        ASSERT_NE(receiver, nullptr);
        ASSERT_TRUE(receiver->InstallGenericHook(
            "On_Health",
            [](void* state, const char*, int, AZ::BehaviorArgument*, int count, AZ::BehaviorArgument* values)
            {
                auto& observation = *static_cast<Received*>(state);
                ASSERT_EQ(count, 1);
                ++observation.m_calls;
                observation.m_value = *values[0].GetAsUnsafe<double>();
            },
            &received));
        AZ::BehaviorArgument receiverAddress;
        receiverAddress.Set(&address);
        ASSERT_TRUE(receiver->Connect(&receiverAddress));
        ScriptDefinedNotificationBus::Event(
            entity.GetId(),
            &ScriptDefinedNotifications::OnPropertyChanged,
            m_definition.m_properties[0].m_id,
            ScriptDefinedRpcValue(uint16_t{ 77 }));
        EXPECT_EQ(received.m_calls, 1);
        EXPECT_EQ(received.m_value, 77);
        receiver->Disconnect();
        bus->m_destroyHandler->Invoke(receiver);
        bridge->Deactivate();
        registration.reset();
        requests.BusDisconnect();
    }
} // namespace ScriptCanvasMultiplayer
AZ_UNIT_TEST_HOOK(DEFAULT_UNIT_TEST_ENV);
