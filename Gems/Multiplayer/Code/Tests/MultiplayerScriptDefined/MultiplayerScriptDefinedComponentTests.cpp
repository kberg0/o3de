/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzNetworking/Serialization/DeltaSerializer.h>
#include <AzNetworking/Serialization/StringifySerializer.h>
#include <CommonNetworkEntitySetup.h>
#include <Multiplayer/Components/MultiplayerScriptDefinedComponent.h>
#include <Multiplayer/Components/MultiplayerScriptDefinedInputComponent.h>
#include <Multiplayer/NetworkInput/NetworkInputChild.h>
#include <Multiplayer/NetworkTime/RewindableFixedVector.h>
#include <cstdio>
#include <limits>

namespace Multiplayer
{
    using namespace MultiplayerTest;

    namespace
    {
        constexpr AZ::u64 TestSchemaFingerprint = 0x56C2F3A0184BE907ULL;
        constexpr uint32_t TestInputValueCount = 2;

        AZStd::vector<ScriptDefinedRpcSignature> TestRpcSignatures()
        {
            return { { 1, { ScriptDefinedRpcType::Int32, ScriptDefinedRpcType::Float, ScriptDefinedRpcType::Vector3 } },
                     { 2, { ScriptDefinedRpcType::Boolean } } };
        }

        class OwnerTrackingScriptDefinedComponent : public MultiplayerScriptDefinedComponent
        {
        public:
            AZ_CLASS_ALLOCATOR(OwnerTrackingScriptDefinedComponent, AZ::ComponentAllocator);

            void SetOwningConnectionId(AzNetworking::ConnectionId connectionId) override
            {
                MultiplayerScriptDefinedComponent::SetOwningConnectionId(connectionId);
                m_lastOwningConnection = connectionId;
            }
            AzNetworking::ConnectionId m_lastOwningConnection = AzNetworking::InvalidConnectionId;
        };

        struct ComponentObservation
        {
            ScriptDefinedRpcPayload m_lastRpc;
            uint32_t m_receivedRpcCount = 0;
            ScriptDefinedInputFrame m_createdInput;
            ScriptDefinedInputFrame m_processedInput;
            uint32_t m_createInputCalls = 0;
            uint32_t m_processInputCalls = 0;
            bool m_wasReprocessing = false;

            MultiplayerScriptDefinedComponent::RpcReceivedEvent::Handler m_rpcHandler{
                [this](AzNetworking::IConnection*, const ScriptDefinedRpcPayload& payload)
                {
                    m_lastRpc = payload;
                    ++m_receivedRpcCount;
                }
            };
            MultiplayerScriptDefinedInputComponent::CreateInputEvent::Handler m_createHandler{ [this](ScriptDefinedInputFrame& frame, float)
                                                                                               {
                                                                                                   frame.m_values = m_createdInput.m_values;
                                                                                                   ++m_createInputCalls;
                                                                                               } };
            MultiplayerScriptDefinedInputComponent::ProcessInputEvent::Handler m_processHandler{
                [this](const ScriptDefinedInputFrame& frame, float, bool reprocessing)
                {
                    m_processedInput = frame;
                    m_wasReprocessing = reprocessing;
                    ++m_processInputCalls;
                }
            };
        };
    } // namespace

    class MultiplayerScriptDefinedComponentTests : public NetworkEntityTests
    {
    public:
        void SetUp() override
        {
            NetworkEntityTests::SetUp();
            AZ::Entity::Reflect(m_serializeContext.get());
            MultiplayerComponent::Reflect(m_serializeContext.get());
            m_console->PerformCommand("net_validateSerializedTypes false");
            m_stateDescriptor.reset(MultiplayerScriptDefinedComponent::CreateDescriptor());
            m_stateDescriptor->Reflect(m_serializeContext.get());
            m_inputDescriptor.reset(MultiplayerScriptDefinedInputComponent::CreateDescriptor());
            m_inputDescriptor->Reflect(m_serializeContext.get());
        }

        void TearDown() override
        {
            m_observations.clear();
            m_testEntities.clear();
            m_inputDescriptor.reset();
            m_stateDescriptor.reset();
            NetworkEntityTests::TearDown();
        }

        AZ::Entity* CreateEntity(NetEntityRole role, bool state = true, bool input = false, bool trackOwnership = false)
        {
            const auto id = aznumeric_cast<uint32_t>(m_testEntities.size() + 1);
            auto info = AZStd::make_unique<EntityInfo>(id, "prototype", NetEntityId{ id }, EntityInfo::Role::None);
            auto* entity = info->m_entity.get();
            auto observation = AZStd::make_unique<ComponentObservation>();
            entity->CreateComponent<AzFramework::TransformComponent>();
            entity->CreateComponent<NetBindComponent>();
            if (state)
            {
                MultiplayerScriptDefinedComponent* component =
                    trackOwnership ? aznew OwnerTrackingScriptDefinedComponent() : aznew MultiplayerScriptDefinedComponent();
                EXPECT_TRUE(entity->AddComponent(component));
                EXPECT_TRUE(component->ConfigureRpcSchema(TestSchemaFingerprint, TestRpcSignatures()));
                component->AddRpcReceivedHandler(observation->m_rpcHandler);
            }
            if (input)
            {
                auto* component = entity->CreateComponent<MultiplayerScriptDefinedInputComponent>();
                EXPECT_TRUE(component->ConfigureInputSchema(TestSchemaFingerprint, TestInputValueCount));
                observation->m_createdInput.m_schemaFingerprint = TestSchemaFingerprint;
                observation->m_createdInput.m_values = { 0.25f, -0.5f };
                component->AddCreateInputHandler(observation->m_createHandler);
                component->AddProcessInputHandler(observation->m_processHandler);
                entity->CreateComponent<TestInputDriverComponent>();
            }
            m_observations[entity] = AZStd::move(observation);
            SetupEntity(info->m_entity, info->m_netId, role);
            entity->Activate();
            m_testEntities.push_back(AZStd::move(info));
            return entity;
        }

        AZ::Entity* CreateDefinedEntity(NetEntityRole role, const MultiplayerScriptDefinedDefinition& definition)
        {
            const auto id = aznumeric_cast<uint32_t>(m_testEntities.size() + 1);
            auto info = AZStd::make_unique<EntityInfo>(id, "defined", NetEntityId{ id }, EntityInfo::Role::None);
            auto* entity = info->m_entity.get();
            entity->CreateComponent<AzFramework::TransformComponent>();
            entity->CreateComponent<NetBindComponent>();
            // Exercise the serialized prefab path, where hidden property banks
            // must be reconstructed from the persisted definition on attachment.
            MultiplayerScriptDefinedComponent authored;
            EXPECT_TRUE(authored.ConfigureDefinition(definition));
            EXPECT_TRUE(entity->AddComponent(m_serializeContext->CloneObject(&authored)));
            if (!definition.m_inputs.empty())
            {
                EXPECT_TRUE(entity->CreateComponent<MultiplayerScriptDefinedInputComponent>()->ConfigureDefinition(definition));
                entity->CreateComponent<TestInputDriverComponent>();
            }
            SetupEntity(info->m_entity, info->m_netId, role);
            entity->Activate();
            m_testEntities.push_back(AZStd::move(info));
            return entity;
        }

        ComponentObservation& Observe(AZ::Entity* entity)
        {
            return *m_observations.at(entity);
        }

        static MultiplayerScriptDefinedComponentController* Controller(AZ::Entity* entity)
        {
            return static_cast<MultiplayerScriptDefinedComponentController*>(
                entity->FindComponent<MultiplayerScriptDefinedComponent>()->GetController());
        }

        static ScriptDefinedRpcPayload MakeRpc()
        {
            ScriptDefinedRpcPayload payload;
            payload.m_schemaFingerprint = TestSchemaFingerprint;
            payload.m_rpcId = 1;
            payload.m_arguments = { int32_t{ 42 }, 0.5f, AZ::Vector3(1.0f, 2.0f, 3.0f) };
            return payload;
        }

        static uint32_t CountDirty(const AzNetworking::IBitset& bits)
        {
            uint32_t count = 0;
            for (uint32_t index = 0; index < bits.GetValidBitCount(); ++index)
            {
                count += bits.GetBit(index) ? 1 : 0;
            }
            return count;
        }

        static ReplicationRecord ChangedRecord(AZ::Entity* entity)
        {
            ReplicationRecord record(NetEntityRole::Client);
            auto* netBind = entity->FindComponent<NetBindComponent>();
            netBind->FillTotalReplicationRecord(record);
            record.Clear();
            netBind->FillReplicationRecord(record);
            return record;
        }

        AZStd::unique_ptr<AZ::ComponentDescriptor> m_stateDescriptor;
        AZStd::unique_ptr<AZ::ComponentDescriptor> m_inputDescriptor;
        AZStd::vector<AZStd::unique_ptr<EntityInfo>> m_testEntities;
        AZStd::unordered_map<AZ::Entity*, AZStd::unique_ptr<ComponentObservation>> m_observations;
    };

    TEST_F(MultiplayerScriptDefinedComponentTests, AuthoredRpcRoutesAndReliabilityUseTheirCompiledEndpoints)
    {
        struct Route
        {
            RpcDeliveryType m_delivery;
            NetEntityRole m_sender;
            NetEntityRole m_receiver;
        };
        const Route routes[] = { { RpcDeliveryType::AuthorityToClient, NetEntityRole::Authority, NetEntityRole::Client },
                                 { RpcDeliveryType::AutonomousToAuthority, NetEntityRole::Autonomous, NetEntityRole::Authority },
                                 { RpcDeliveryType::AuthorityToAutonomous, NetEntityRole::Authority, NetEntityRole::Autonomous },
                                 { RpcDeliveryType::ServerToAuthority, NetEntityRole::Server, NetEntityRole::Authority } };
        for (const auto& route : routes)
        {
            for (bool reliable : { false, true })
            {
                MultiplayerScriptDefinedDefinition definition;
                ScriptDefinedRpcDefinition rpc;
                rpc.m_name = "TestRpc";
                rpc.m_delivery = route.m_delivery;
                rpc.m_isReliable = reliable;
                ScriptDefinedParameter parameter;
                parameter.m_name = "Argument";
                parameter.m_type = ScriptDefinedRpcType::Int64;
                rpc.m_parameters.push_back(parameter);
                definition.m_rpcs.push_back(rpc);
                auto* sender = CreateDefinedEntity(route.m_sender, definition);
                auto* receiver = CreateDefinedEntity(route.m_receiver, definition);
                NetworkEntityRpcMessage sent;
                unsigned sendCount = 0, receivedCount = 0;
                AZ::Event<NetworkEntityRpcMessage&>::Handler send(
                    [&](NetworkEntityRpcMessage& message)
                    {
                        sent = message;
                        ++sendCount;
                    });
                auto* binding = sender->FindComponent<NetBindComponent>();
                switch (route.m_delivery)
                {
                case RpcDeliveryType::AuthorityToClient:
                    send.Connect(binding->GetSendAuthorityToClientRpcEvent());
                    break;
                case RpcDeliveryType::AutonomousToAuthority:
                    send.Connect(binding->GetSendAutonomousToAuthorityRpcEvent());
                    break;
                case RpcDeliveryType::AuthorityToAutonomous:
                    send.Connect(binding->GetSendAuthorityToAutonomousRpcEvent());
                    break;
                case RpcDeliveryType::ServerToAuthority:
                    send.Connect(binding->GetSendServerToAuthorityRpcEvent());
                    break;
                default:
                    FAIL();
                }
                MultiplayerScriptDefinedComponent::RpcReceivedEvent::Handler receive(
                    [&](AzNetworking::IConnection*, const ScriptDefinedRpcPayload& payload)
                    {
                        ++receivedCount;
                        EXPECT_EQ(AZStd::get<int64_t>(payload.m_arguments[0]), std::numeric_limits<int64_t>::min());
                    });
                receiver->FindComponent<MultiplayerScriptDefinedComponent>()->AddRpcReceivedHandler(receive);
                ASSERT_TRUE(sender->FindComponent<MultiplayerScriptDefinedComponent>()->InvokeRpc(
                    rpc.m_id, { ScriptDefinedRpcValue(std::numeric_limits<int64_t>::min()) }));
                ASSERT_EQ(sendCount, 1);
                EXPECT_EQ(sent.GetReliability(), reliable ? ReliabilityType::Reliable : ReliabilityType::Unreliable);
                ASSERT_TRUE(receiver->FindComponent<MultiplayerScriptDefinedComponent>()->HandleRpcMessage(nullptr, route.m_sender, sent));
                EXPECT_EQ(receivedCount, 1);
            }
        }
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, AuthoredTypedPropertiesReplicateWithDefaultsAndRejectWrongTypesAndRoles)
    {
        MultiplayerScriptDefinedDefinition definition;
        const char* text[] = { "true",
                               "-2147483648",
                               "1.5",
                               "1,2,3",
                               "-128",
                               "255",
                               "-32768",
                               "65535",
                               "4294967295",
                               "-9223372036854775808",
                               "18446744073709551615",
                               "1.25",
                               "2,3",
                               "0,0,0,1",
                               "hello",
                               "42" };
        for (unsigned type = 0; type < ScriptDefinedPropertyBankCount; ++type)
        {
            ScriptDefinedField field;
            field.m_name = AZStd::string::format("Property%u", type);
            field.m_type = static_cast<ScriptDefinedRpcType>(type);
            definition.m_properties.push_back(field);
        }
        auto* authority = CreateDefinedEntity(NetEntityRole::Authority, definition);
        auto* client = CreateDefinedEntity(NetEntityRole::Client, definition);
        auto* sender = authority->FindComponent<MultiplayerScriptDefinedComponent>();
        auto* receiver = client->FindComponent<MultiplayerScriptDefinedComponent>();
        for (size_t type = 0; type < definition.m_properties.size(); ++type)
        {
            const auto& field = definition.m_properties[type];
            auto parsed = ParseScriptDefinedValue(field.m_type, text[type]);
            ASSERT_TRUE(parsed);
            EXPECT_TRUE(sender->WriteProperty(field.m_id, parsed.GetValue()));
            EXPECT_FALSE(receiver->WriteProperty(field.m_id, parsed.GetValue()));
        }
        EXPECT_FALSE(sender->WriteProperty(definition.m_properties[0].m_id, float{ 1 }));
        auto record = ChangedRecord(authority);
        AZStd::array<uint8_t, 4096> buffer{};
        AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(record.Serialize(writer));
        ASSERT_TRUE(authority->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(record, writer));
        ReplicationRecord decoded(NetEntityRole::Client);
        AzNetworking::NetworkOutputSerializer reader(buffer.data(), writer.GetSize());
        ASSERT_TRUE(decoded.Serialize(reader));
        ASSERT_TRUE(client->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(decoded, reader));
        for (size_t type = 0; type < definition.m_properties.size(); ++type)
        {
            ScriptDefinedRpcValue value;
            ASSERT_TRUE(receiver->ReadProperty(definition.m_properties[type].m_id, value));
            EXPECT_EQ(value, ParseScriptDefinedValue(definition.m_properties[type].m_type, text[type]).GetValue());
        }
        EXPECT_FALSE(sender->ConfigureDefinition(definition));
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, SerializedDefinitionInitializesDefaultsAndOnlyDeclaredPredictionBits)
    {
        MultiplayerScriptDefinedDefinition definition;
        ScriptDefinedField health;
        health.m_name = "Health";
        health.m_type = ScriptDefinedRpcType::UInt16;
        health.m_defaultValue = "100";
        ScriptDefinedField move;
        move.m_name = "Move";
        move.m_type = ScriptDefinedRpcType::Float;
        ScriptDefinedField predicted = move;
        predicted.m_id = AZ::Uuid::CreateRandom();
        predicted.m_name = "Predicted";
        predicted.m_defaultValue = "1.25";
        predicted.m_predictable = true;
        definition.m_properties = { health, move, predicted };
        auto* entity = CreateDefinedEntity(NetEntityRole::Autonomous, definition);
        auto* state = entity->FindComponent<MultiplayerScriptDefinedComponent>();
        ScriptDefinedRpcValue value;
        ASSERT_TRUE(state->ReadProperty(health.m_id, value));
        EXPECT_EQ(AZStd::get<uint16_t>(value), 100);
        ASSERT_TRUE(state->ReadProperty(predicted.m_id, value));
        EXPECT_EQ(AZStd::get<float>(value), 1.25f);
        EXPECT_EQ(CountDirty(entity->FindComponent<NetBindComponent>()->GetPredictableRecord().m_authorityToClient), 2);
        EXPECT_FALSE(state->WriteProperty(health.m_id, uint16_t{ 93 }));
        EXPECT_FALSE(state->WriteProperty(move.m_id, 6.5f));
        EXPECT_TRUE(state->WriteProperty(predicted.m_id, 2.5f));
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, MismatchedPropertySchemaRejectsBeforeApplyingAnyValues)
    {
        MultiplayerScriptDefinedDefinition first;
        ScriptDefinedField field;
        field.m_name = "Health";
        field.m_defaultValue = "10";
        first.m_properties.push_back(field);
        auto second = first;
        second.m_properties[0].m_type = ScriptDefinedRpcType::Int32;
        auto* sender = CreateDefinedEntity(NetEntityRole::Authority, first);
        auto* receiver = CreateDefinedEntity(NetEntityRole::Client, second);
        auto record = ChangedRecord(sender);
        AZStd::array<uint8_t, 512> buffer{};
        AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(record.Serialize(writer));
        ASSERT_TRUE(sender->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(record, writer));
        ReplicationRecord decoded(NetEntityRole::Client);
        AzNetworking::NetworkOutputSerializer reader(buffer.data(), writer.GetSize());
        ASSERT_TRUE(decoded.Serialize(reader));
        EXPECT_FALSE(receiver->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(decoded, reader));
        ScriptDefinedRpcValue unchanged;
        ASSERT_TRUE(receiver->FindComponent<MultiplayerScriptDefinedComponent>()->ReadProperty(field.m_id, unchanged));
        EXPECT_EQ(AZStd::get<int32_t>(unchanged), 10);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, TypedInputsAreWritableOnlyDuringGatherAndReadableDuringReplay)
    {
        MultiplayerScriptDefinedDefinition definition;
        ScriptDefinedField field;
        field.m_name = "Jump";
        field.m_type = ScriptDefinedRpcType::Boolean;
        definition.m_inputs.push_back(field);
        auto* entity = CreateDefinedEntity(NetEntityRole::Autonomous, definition);
        auto* component = entity->FindComponent<MultiplayerScriptDefinedInputComponent>();
        class InputObserver : public ScriptDefinedNotificationBus::Handler
        {
        public:
            AZ::EntityId m_entityId;
            AZ::Uuid m_id;
            bool m_processed = false;
            bool m_replaying = false;
            void OnCreateInput(float) override
            {
                bool success = false;
                ScriptDefinedInputRequestBus::EventResult(
                    success, m_entityId, &ScriptDefinedInputRequests::WriteInput, m_id, ScriptDefinedRpcValue(true));
                EXPECT_TRUE(success);
            }
            void OnProcessInput(float, bool replaying) override
            {
                ScriptDefinedRpcValue value;
                bool success = false;
                ScriptDefinedInputRequestBus::EventResult(success, m_entityId, &ScriptDefinedInputRequests::ReadInput, m_id, value);
                EXPECT_TRUE(success);
                EXPECT_TRUE(AZStd::get<bool>(value));
                ScriptDefinedInputRequestBus::EventResult(
                    success, m_entityId, &ScriptDefinedInputRequests::WriteInput, m_id, ScriptDefinedRpcValue(false));
                EXPECT_FALSE(success);
                m_processed = true;
                m_replaying = replaying;
            }
        } observer;
        observer.m_entityId = entity->GetId();
        observer.m_id = field.m_id;
        observer.BusConnect(entity->GetId());
        EXPECT_FALSE(component->WriteInput(field.m_id, ScriptDefinedRpcValue(true)));
        NetworkInputChild storage;
        auto& input = storage.GetNetworkInput();
        auto* binding = entity->FindComponent<NetBindComponent>();
        input.AttachNetBindComponent(binding);
        binding->CreateInput(input, 0.016f);
        binding->ProcessInput(input, 0.016f);
        EXPECT_TRUE(observer.m_processed);
        EXPECT_FALSE(observer.m_replaying);
        binding->ReprocessInput(input, 0.016f);
        EXPECT_TRUE(observer.m_replaying);
        ScriptDefinedRpcValue value;
        EXPECT_FALSE(component->ReadInput(field.m_id, value));
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, ReflectionWithEditContextRegistersConcreteClassesBeforeGeneratedEditData)
    {
        AZ::SerializeContext context;
        AZ::Entity::Reflect(&context);
        MultiplayerComponent::Reflect(&context);
        context.CreateEditContext();
        MultiplayerScriptDefinedComponent::Reflect(&context);
        MultiplayerScriptDefinedInputComponent::Reflect(&context);
        ASSERT_NE(context.FindClassData(azrtti_typeid<MultiplayerScriptDefinedComponent>()), nullptr);
        EXPECT_NE(context.FindClassData(azrtti_typeid<MultiplayerScriptDefinedComponent>())->m_editData, nullptr);
        ASSERT_NE(context.FindClassData(azrtti_typeid<MultiplayerScriptDefinedInputComponent>()), nullptr);
        EXPECT_NE(context.FindClassData(azrtti_typeid<MultiplayerScriptDefinedInputComponent>())->m_editData, nullptr);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, DefinitionValidatesTypesBoundsAndStableMemberMappings)
    {
        MultiplayerScriptDefinedDefinition definition;
        for (unsigned type = 0; type < ScriptDefinedPropertyBankCount; ++type)
        {
            ScriptDefinedField field;
            field.m_name = AZStd::string::format("Field%u", type);
            field.m_type = static_cast<ScriptDefinedRpcType>(type);
            definition.m_properties.push_back(field);
        }
        ASSERT_TRUE(definition.Validate());
        const auto fingerprint = definition.GetFingerprint();
        auto reordered = definition;
        AZStd::reverse(reordered.m_properties.begin(), reordered.m_properties.end());
        EXPECT_EQ(reordered.GetFingerprint(), fingerprint);
        for (const auto& field : definition.m_properties)
        {
            EXPECT_EQ(reordered.GetPropertySlot(field.m_id), definition.GetPropertySlot(field.m_id));
        }
        reordered.m_properties[0].m_name = "Renamed";
        EXPECT_EQ(reordered.GetFingerprint(), fingerprint);
        reordered.m_properties[0].m_type = ScriptDefinedRpcType::Float;
        EXPECT_NE(reordered.GetFingerprint(), fingerprint);
        definition.m_properties.push_back(definition.m_properties[0]);
        EXPECT_FALSE(definition.Validate());
        EXPECT_TRUE(ParseScriptDefinedValue(ScriptDefinedRpcType::UInt64, "18446744073709551615"));
        EXPECT_TRUE(ParseScriptDefinedValue(ScriptDefinedRpcType::Int64, "-9223372036854775808"));
        EXPECT_FALSE(ParseScriptDefinedValue(ScriptDefinedRpcType::Int8, "128"));
        EXPECT_FALSE(ParseScriptDefinedValue(ScriptDefinedRpcType::UInt16, "-1"));
        EXPECT_FALSE(ParseScriptDefinedValue(ScriptDefinedRpcType::Float, "nan"));
        EXPECT_FALSE(ParseScriptDefinedValue(ScriptDefinedRpcType::Vector3, "1,2"));
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, TypedValuesRoundTripEveryNativeWireType)
    {
        for (unsigned type = 0; type < ScriptDefinedPropertyBankCount; ++type)
        {
            ScriptDefinedInputFrame source;
            source.m_isTyped = true;
            source.m_schemaFingerprint = TestSchemaFingerprint;
            source.m_typedValues.push_back(DefaultScriptDefinedValue(static_cast<ScriptDefinedRpcType>(type)));
            AZStd::array<uint8_t, 512> buffer{};
            AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
            ASSERT_TRUE(source.Serialize(writer));
            ScriptDefinedInputFrame decoded;
            AzNetworking::NetworkOutputSerializer reader(buffer.data(), writer.GetSize());
            ASSERT_TRUE(decoded.Serialize(reader));
            EXPECT_EQ(source, decoded);
        }
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, TypedInputVectorRejectsOversizedLocalFramesBeforeWriting)
    {
        ScriptDefinedInputFrame frame;
        frame.m_isTyped = true;
        frame.m_typedValues.resize(ScriptDefinedInputCapacity + 1, ScriptDefinedRpcValue(false));
        EXPECT_FALSE(frame.IsValid());
        AZStd::array<uint8_t, 512> buffer{};
        AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        EXPECT_FALSE(frame.Serialize(writer));
        EXPECT_FALSE(writer.IsValid());
        EXPECT_EQ(writer.GetSize(), 0);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, EmptyBankReservesCapacityBitsWithoutSendingValues)
    {
        auto* entity = CreateEntity(NetEntityRole::Authority);
        const auto record = ChangedRecord(entity);
        EXPECT_EQ(record.m_authorityToClient.GetValidBitCount(), ScriptDefinedPropertyRecordBits);
        EXPECT_FALSE(record.HasChanges());
        AZStd::array<uint8_t, 512> buffer{};
        AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        auto writableRecord = record;
        EXPECT_TRUE(entity->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(writableRecord, writer));
        EXPECT_EQ(writer.GetSize(), 0);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, ComponentsAreRegisteredByTheRuntimeGem)
    {
        MultiplayerScriptDefinedComponent state;
        MultiplayerScriptDefinedInputComponent input;
        auto* registry = GetMultiplayerComponentRegistry();
        EXPECT_STREQ(registry->GetComponentGemName(state.GetNetComponentId()), "Multiplayer");
        EXPECT_STREQ(registry->GetComponentName(state.GetNetComponentId()), "MultiplayerScriptDefinedComponent");
        EXPECT_STREQ(registry->GetComponentGemName(input.GetNetComponentId()), "Multiplayer");
        EXPECT_STREQ(registry->GetComponentName(input.GetNetComponentId()), "MultiplayerScriptDefinedInputComponent");
        EXPECT_EQ(registry->AllocateComponentInput(state.GetNetComponentId()), nullptr);
        EXPECT_NE(registry->AllocateComponentInput(input.GetNetComponentId()), nullptr);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, InvalidSchemasAreRejectedAndConfigurationLocksAtNetworkAttachment)
    {
        MultiplayerScriptDefinedComponent state;
        auto signatures = TestRpcSignatures();
        EXPECT_FALSE(state.ConfigureRpcSchema(0, signatures));
        signatures[1].m_rpcId = signatures[0].m_rpcId;
        EXPECT_FALSE(state.ConfigureRpcSchema(TestSchemaFingerprint, signatures));
        signatures = TestRpcSignatures();
        signatures[0].m_rpcId = 0;
        EXPECT_FALSE(state.ConfigureRpcSchema(TestSchemaFingerprint, signatures));
        signatures = TestRpcSignatures();
        signatures[0].m_parameterTypes[0] = static_cast<ScriptDefinedRpcType>(255);
        EXPECT_FALSE(state.ConfigureRpcSchema(TestSchemaFingerprint, signatures));
        signatures.resize(ScriptDefinedRpcCapacity + 1);
        EXPECT_FALSE(state.ConfigureRpcSchema(TestSchemaFingerprint, signatures));
        EXPECT_TRUE(state.ConfigureRpcSchema(TestSchemaFingerprint, TestRpcSignatures()));

        MultiplayerScriptDefinedInputComponent input;
        EXPECT_FALSE(input.IsConfigured());
        EXPECT_FALSE(input.ConfigureInputSchema(0, TestInputValueCount));
        EXPECT_FALSE(input.ConfigureInputSchema(TestSchemaFingerprint, 0));
        EXPECT_FALSE(input.ConfigureInputSchema(TestSchemaFingerprint, ScriptDefinedInputCapacity + 1));
        EXPECT_TRUE(input.ConfigureInputSchema(TestSchemaFingerprint, TestInputValueCount));
        EXPECT_TRUE(input.IsConfigured());

        auto* entity = CreateEntity(NetEntityRole::Authority, true, true);
        EXPECT_FALSE(
            entity->FindComponent<MultiplayerScriptDefinedComponent>()->ConfigureRpcSchema(TestSchemaFingerprint + 1, TestRpcSignatures()));
        EXPECT_FALSE(entity->FindComponent<MultiplayerScriptDefinedInputComponent>()->ConfigureInputSchema(TestSchemaFingerprint, 1));
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, SerializedConfigurationSurvivesCloningWithoutTestFixtures)
    {
        MultiplayerScriptDefinedComponent source;
        ASSERT_TRUE(source.ConfigureRpcSchema(TestSchemaFingerprint + 7, { { 7, { ScriptDefinedRpcType::Float } } }));
        AZStd::unique_ptr<MultiplayerScriptDefinedComponent> clone(m_serializeContext->CloneObject(&source));
        ASSERT_NE(clone, nullptr);
        uint32_t received = 0;
        MultiplayerScriptDefinedComponent::RpcReceivedEvent::Handler handler(
            [&received](AzNetworking::IConnection*, const ScriptDefinedRpcPayload&)
            {
                ++received;
            });
        clone->AddRpcReceivedHandler(handler);
        ScriptDefinedRpcPayload payload;
        payload.m_schemaFingerprint = TestSchemaFingerprint + 7;
        payload.m_rpcId = 7;
        payload.m_arguments = { 0.5f };
        clone->HandleDispatch(nullptr, payload);
        EXPECT_EQ(received, 1);

        MultiplayerScriptDefinedInputComponent input;
        ASSERT_TRUE(input.ConfigureInputSchema(TestSchemaFingerprint, TestInputValueCount));
        AZStd::unique_ptr<MultiplayerScriptDefinedInputComponent> inputClone(m_serializeContext->CloneObject(&input));
        ASSERT_NE(inputClone, nullptr);
        EXPECT_TRUE(inputClone->IsConfigured());
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, CheckedRpcSendRejectsInvalidPayloadsAndWrongRoles)
    {
        auto* authority = CreateEntity(NetEntityRole::Authority);
        auto* state = authority->FindComponent<MultiplayerScriptDefinedComponent>();
        uint32_t sends = 0;
        AZ::Event<NetworkEntityRpcMessage&>::Handler handler(
            [&sends](NetworkEntityRpcMessage&)
            {
                ++sends;
            });
        handler.Connect(authority->FindComponent<NetBindComponent>()->GetSendAuthorityToClientRpcEvent());
        EXPECT_TRUE(state->SendRpc(MakeRpc()));
        auto invalid = MakeRpc();
        invalid.m_schemaFingerprint = 0;
        EXPECT_FALSE(state->SendRpc(invalid));
        invalid = MakeRpc();
        invalid.m_arguments[1] = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(state->SendRpc(invalid));
        invalid = MakeRpc();
        invalid.m_rpcId = 99;
        EXPECT_FALSE(state->SendRpc(invalid));
        auto* autonomous = CreateEntity(NetEntityRole::Autonomous);
        EXPECT_FALSE(autonomous->FindComponent<MultiplayerScriptDefinedComponent>()->SendRpc(MakeRpc()));
        EXPECT_EQ(sends, 1);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, InvalidInputCallbacksProduceNeutralInputAndInvalidLocalFramesAreNotProcessed)
    {
        auto* entity = CreateEntity(NetEntityRole::Autonomous, true, true);
        auto* inputComponent = entity->FindComponent<MultiplayerScriptDefinedInputComponent>();
        Observe(entity).m_createHandler.Disconnect();
        MultiplayerScriptDefinedInputComponent::CreateInputEvent::Handler handler(
            [](ScriptDefinedInputFrame& frame, float)
            {
                frame.m_schemaFingerprint = 0;
                frame.m_values.clear();
            });
        inputComponent->AddCreateInputHandler(handler);
        NetworkInputChild storage;
        auto& input = storage.GetNetworkInput();
        auto* netBind = entity->FindComponent<NetBindComponent>();
        input.AttachNetBindComponent(netBind);
        netBind->CreateInput(input, 0.016f);
        auto* frame = input.FindComponentInput<MultiplayerScriptDefinedInputComponentNetworkInput>();
        ASSERT_NE(frame, nullptr);
        EXPECT_EQ(frame->m_frame.m_schemaFingerprint, TestSchemaFingerprint);
        ASSERT_EQ(frame->m_frame.m_values.size(), TestInputValueCount);
        EXPECT_FLOAT_EQ(frame->m_frame.m_values[0], 0.0f);
        EXPECT_FLOAT_EQ(frame->m_frame.m_values[1], 0.0f);
        frame->m_frame.m_values[0] = std::numeric_limits<float>::quiet_NaN();
        netBind->ProcessInput(input, 0.016f);
        EXPECT_EQ(Observe(entity).m_processInputCalls, 0);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, SingleSlotDeltaRoundTripsWithoutResendingOtherSlots)
    {
        auto* authority = CreateEntity(NetEntityRole::Authority);
        auto* client = CreateEntity(NetEntityRole::Client);
        auto* controller = Controller(authority);
        ASSERT_TRUE(controller->ValuesPushBack(1.0f));
        ASSERT_TRUE(controller->ValuesPushBack(2.0f));

        AZStd::array<uint8_t, 512> buffer{};
        auto initial = ChangedRecord(authority);
        AzNetworking::NetworkInputSerializer initialWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(initial.Serialize(initialWriter));
        ASSERT_TRUE(authority->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(initial, initialWriter));
        AzNetworking::NetworkOutputSerializer initialReader(buffer.data(), initialWriter.GetSize());
        ReplicationRecord initialDecoded(NetEntityRole::Client);
        ASSERT_TRUE(initialDecoded.Serialize(initialReader));
        ASSERT_TRUE(client->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(initialDecoded, initialReader));
        m_networkEntityManager->NotifyEntitiesDirtied();

        ASSERT_TRUE(controller->TrySetValue(1, 9.0f));
        auto delta = ChangedRecord(authority);
        EXPECT_EQ(CountDirty(delta.m_authorityToClient), 1);
        EXPECT_FALSE(delta.m_authorityToClient.GetBit(ScriptDefinedPropertyCapacity));
        AzNetworking::NetworkInputSerializer deltaWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(delta.Serialize(deltaWriter));
        const auto recordBytes = deltaWriter.GetSize();
        ASSERT_TRUE(authority->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(delta, deltaWriter));
        EXPECT_EQ(deltaWriter.GetSize() - recordBytes, sizeof(AZ::u64) + sizeof(float));
        AzNetworking::NetworkOutputSerializer deltaReader(buffer.data(), deltaWriter.GetSize());
        ReplicationRecord deltaDecoded(NetEntityRole::Client);
        ASSERT_TRUE(deltaDecoded.Serialize(deltaReader));
        ASSERT_TRUE(client->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(deltaDecoded, deltaReader));
        const auto* state = client->FindComponent<MultiplayerScriptDefinedComponent>();
        EXPECT_FLOAT_EQ(state->GetValues(0), 1.0f);
        EXPECT_FLOAT_EQ(state->GetValues(1), 9.0f);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, UnchangedAndInvalidWritesDoNotDirtyBank)
    {
        auto* entity = CreateEntity(NetEntityRole::Authority);
        auto* controller = Controller(entity);
        ASSERT_TRUE(controller->ValuesPushBack(1.0f));
        m_networkEntityManager->NotifyEntitiesDirtied();
        EXPECT_TRUE(controller->TrySetValue(0, 1.0f));
        EXPECT_FALSE(controller->TrySetValue(1, 2.0f));
        EXPECT_FALSE(controller->TrySetValue(AZStd::numeric_limits<uint32_t>::max(), 2.0f));
        EXPECT_FALSE(controller->TrySetValue(0, std::numeric_limits<float>::quiet_NaN()));
        EXPECT_FALSE(ChangedRecord(entity).HasChanges());
        auto* autonomous = CreateEntity(NetEntityRole::Autonomous);
        EXPECT_FALSE(Controller(autonomous)->TrySetValue(0, 1.0f));
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, BankCapacityIsBoundedAndSizeChangesRoundTrip)
    {
        auto* authority = CreateEntity(NetEntityRole::Authority);
        auto* client = CreateEntity(NetEntityRole::Client);
        auto* controller = Controller(authority);
        for (uint32_t index = 0; index < ScriptDefinedPropertyCapacity; ++index)
        {
            ASSERT_TRUE(controller->ValuesPushBack(aznumeric_cast<float>(index)));
        }
        EXPECT_FALSE(controller->ValuesPushBack(99.0f));
        EXPECT_EQ(CountDirty(ChangedRecord(authority).m_authorityToClient), ScriptDefinedPropertyCapacity + 1);
        ASSERT_TRUE(controller->ValuesPopBack());
        controller->ValuesClear();
        auto record = ChangedRecord(authority);
        AZStd::array<uint8_t, 512> buffer{};
        AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(authority->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(record, writer));
        record.ResetConsumedBits();
        AzNetworking::NetworkOutputSerializer reader(buffer.data(), writer.GetSize());
        ASSERT_TRUE(client->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(record, reader));
        EXPECT_EQ(client->FindComponent<MultiplayerScriptDefinedComponent>()->ValuesGetSize(), 0);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, TypedVectorsPreserveBooleansAndEveryIntegerWidth)
    {
        using ControllerType = MultiplayerScriptDefinedComponentController;
        using ComponentType = MultiplayerScriptDefinedComponent;
        auto verify = [this](const char* name, auto append, auto set, auto get, auto minimum, auto maximum)
        {
            SCOPED_TRACE(name);
            auto* authority = CreateEntity(NetEntityRole::Authority);
            auto* client = CreateEntity(NetEntityRole::Client);
            auto* controller = Controller(authority);
            auto* receiver = client->FindComponent<ComponentType>();
            ASSERT_TRUE((controller->*append)(minimum));
            ASSERT_TRUE((controller->*append)(maximum));
            AZStd::array<uint8_t, 512> buffer{};
            auto initial = ChangedRecord(authority);
            EXPECT_EQ(CountDirty(initial.m_authorityToClient), 3);
            AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
            ASSERT_TRUE(initial.Serialize(writer));
            const auto initialRecordBytes = writer.GetSize();
            ASSERT_TRUE(authority->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(initial, writer));
            EXPECT_EQ(writer.GetSize() - initialRecordBytes, sizeof(AZ::u64) + 1 + 2 * sizeof(minimum));
            ReplicationRecord decoded(NetEntityRole::Client);
            AzNetworking::NetworkOutputSerializer reader(buffer.data(), writer.GetSize());
            ASSERT_TRUE(decoded.Serialize(reader));
            ASSERT_TRUE(client->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(decoded, reader));
            EXPECT_EQ((receiver->*get)(0), minimum);
            EXPECT_EQ((receiver->*get)(1), maximum);
            m_networkEntityManager->NotifyEntitiesDirtied();

            (controller->*set)(0, maximum);
            auto delta = ChangedRecord(authority);
            EXPECT_EQ(CountDirty(delta.m_authorityToClient), 1);
            AzNetworking::NetworkInputSerializer deltaWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
            ASSERT_TRUE(delta.Serialize(deltaWriter));
            const auto deltaRecordBytes = deltaWriter.GetSize();
            ASSERT_TRUE(authority->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(delta, deltaWriter));
            EXPECT_EQ(deltaWriter.GetSize() - deltaRecordBytes, sizeof(AZ::u64) + sizeof(minimum));
            ReplicationRecord decodedDelta(NetEntityRole::Client);
            AzNetworking::NetworkOutputSerializer deltaReader(buffer.data(), deltaWriter.GetSize());
            ASSERT_TRUE(decodedDelta.Serialize(deltaReader));
            ASSERT_TRUE(client->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(decodedDelta, deltaReader));
            EXPECT_EQ((receiver->*get)(0), maximum);
            EXPECT_EQ((receiver->*get)(1), maximum);
            m_networkEntityManager->NotifyEntitiesDirtied();
            (controller->*set)(0, maximum);
            EXPECT_FALSE(ChangedRecord(authority).HasChanges());
        };

        verify("bool", &ControllerType::BoolValuesPushBack, &ControllerType::SetBoolValues, &ComponentType::GetBoolValues, false, true);
        verify(
            "int8",
            &ControllerType::Int8ValuesPushBack,
            &ControllerType::SetInt8Values,
            &ComponentType::GetInt8Values,
            AZStd::numeric_limits<int8_t>::min(),
            AZStd::numeric_limits<int8_t>::max());
        verify(
            "uint8",
            &ControllerType::UInt8ValuesPushBack,
            &ControllerType::SetUInt8Values,
            &ComponentType::GetUInt8Values,
            AZStd::numeric_limits<uint8_t>::min(),
            AZStd::numeric_limits<uint8_t>::max());
        verify(
            "int16",
            &ControllerType::Int16ValuesPushBack,
            &ControllerType::SetInt16Values,
            &ComponentType::GetInt16Values,
            AZStd::numeric_limits<int16_t>::min(),
            AZStd::numeric_limits<int16_t>::max());
        verify(
            "uint16",
            &ControllerType::UInt16ValuesPushBack,
            &ControllerType::SetUInt16Values,
            &ComponentType::GetUInt16Values,
            AZStd::numeric_limits<uint16_t>::min(),
            AZStd::numeric_limits<uint16_t>::max());
        verify(
            "int32",
            &ControllerType::Int32ValuesPushBack,
            &ControllerType::SetInt32Values,
            &ComponentType::GetInt32Values,
            AZStd::numeric_limits<int32_t>::min(),
            AZStd::numeric_limits<int32_t>::max());
        verify(
            "uint32",
            &ControllerType::UInt32ValuesPushBack,
            &ControllerType::SetUInt32Values,
            &ComponentType::GetUInt32Values,
            AZStd::numeric_limits<uint32_t>::min(),
            AZStd::numeric_limits<uint32_t>::max());
        verify(
            "int64",
            &ControllerType::Int64ValuesPushBack,
            &ControllerType::SetInt64Values,
            &ComponentType::GetInt64Values,
            AZStd::numeric_limits<int64_t>::min(),
            AZStd::numeric_limits<int64_t>::max());
        verify(
            "uint64",
            &ControllerType::UInt64ValuesPushBack,
            &ControllerType::SetUInt64Values,
            &ComponentType::GetUInt64Values,
            AZStd::numeric_limits<uint64_t>::min(),
            AZStd::numeric_limits<uint64_t>::max());
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, TypedVectorMeasurements)
    {
        using ControllerType = MultiplayerScriptDefinedComponentController;
        auto measure = [this](const char* name, auto append, auto set, auto minimum, auto maximum)
        {
            SCOPED_TRACE(name);
            using ValueType = decltype(minimum);
            auto* authority = CreateEntity(NetEntityRole::Authority);
            auto* controller = Controller(authority);
            AZStd::array<uint8_t, 512> buffer{};
            auto empty = ChangedRecord(authority);
            AzNetworking::NetworkInputSerializer emptyWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
            ASSERT_TRUE(authority->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(empty, emptyWriter));
            EXPECT_EQ(emptyWriter.GetSize(), 0);

            ASSERT_TRUE((controller->*append)(maximum));
            auto added = ChangedRecord(authority);
            AzNetworking::NetworkInputSerializer addedWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
            ASSERT_TRUE(authority->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(added, addedWriter));
            m_networkEntityManager->NotifyEntitiesDirtied();
            (controller->*set)(0, minimum);
            auto delta = ChangedRecord(authority);
            AzNetworking::NetworkInputSerializer deltaWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
            auto record = delta;
            ASSERT_TRUE(record.Serialize(deltaWriter));
            const auto recordBytes = deltaWriter.GetSize();
            ASSERT_TRUE(authority->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(record, deltaWriter));
            std::printf(
                "typed_vector,type=%s,capacity=16,bank_bytes=%zu,active=1,empty_value_bytes=%u,"
                "added_value_bytes=%u,delta_value_bytes=%u,set_bits=%u,entity_reserved_bits=%u,entity_record_bytes=%u\n",
                name,
                sizeof(AZStd::fixed_vector<ValueType, ScriptDefinedPropertyCapacity>),
                emptyWriter.GetSize(),
                addedWriter.GetSize(),
                deltaWriter.GetSize() - recordBytes,
                CountDirty(delta.m_authorityToClient),
                delta.m_authorityToClient.GetValidBitCount(),
                recordBytes);
            EXPECT_EQ(addedWriter.GetSize(), sizeof(AZ::u64) + 1 + sizeof(ValueType));
            EXPECT_EQ(deltaWriter.GetSize() - recordBytes, sizeof(AZ::u64) + sizeof(ValueType));
        };

        measure("bool", &ControllerType::BoolValuesPushBack, &ControllerType::SetBoolValues, false, true);
        measure("int8", &ControllerType::Int8ValuesPushBack, &ControllerType::SetInt8Values, int8_t{ 0 }, int8_t{ 1 });
        measure("uint8", &ControllerType::UInt8ValuesPushBack, &ControllerType::SetUInt8Values, uint8_t{ 0 }, uint8_t{ 1 });
        measure("int16", &ControllerType::Int16ValuesPushBack, &ControllerType::SetInt16Values, int16_t{ 0 }, int16_t{ 1 });
        measure("uint16", &ControllerType::UInt16ValuesPushBack, &ControllerType::SetUInt16Values, uint16_t{ 0 }, uint16_t{ 1 });
        measure("int32", &ControllerType::Int32ValuesPushBack, &ControllerType::SetInt32Values, int32_t{ 0 }, int32_t{ 1 });
        measure("uint32", &ControllerType::UInt32ValuesPushBack, &ControllerType::SetUInt32Values, uint32_t{ 0 }, uint32_t{ 1 });
        measure("int64", &ControllerType::Int64ValuesPushBack, &ControllerType::SetInt64Values, int64_t{ 0 }, int64_t{ 1 });
        measure("uint64", &ControllerType::UInt64ValuesPushBack, &ControllerType::SetUInt64Values, uint64_t{ 0 }, uint64_t{ 1 });
        measure("float", &ControllerType::ValuesPushBack, &ControllerType::SetValues, 0.0f, 1.0f);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, OmittedInputHasNoPayloadCallbacksOrAdditionalWireBytes)
    {
        auto* baseline = CreateEntity(NetEntityRole::Autonomous, false);
        auto* entity = CreateEntity(NetEntityRole::Autonomous);
        auto* netBind = entity->FindComponent<NetBindComponent>();
        EXPECT_TRUE(netBind->AllocateComponentInputs().empty());
        NetworkInputChild inputStorage;
        auto& input = inputStorage.GetNetworkInput();
        input.AttachNetBindComponent(netBind);
        netBind->CreateInput(input, 0.016f);
        netBind->ProcessInput(input, 0.016f);
        netBind->ReprocessInput(input, 0.016f);
        EXPECT_FALSE(Controller(entity)->IsInputEnabled());
        NetworkInputChild baselineStorage;
        auto& baselineInput = baselineStorage.GetNetworkInput();
        baselineInput.AttachNetBindComponent(baseline->FindComponent<NetBindComponent>());
        AZStd::array<uint8_t, 512> buffer{};
        AzNetworking::NetworkInputSerializer baselineWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(baselineInput.Serialize(baselineWriter));
        ASSERT_TRUE(input.Serialize(writer));
        EXPECT_EQ(writer.GetSize(), baselineWriter.GetSize());
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, InputOptOutPreservesOwnershipUpdates)
    {
        auto* entity = CreateEntity(NetEntityRole::Authority, true, false, true);
        entity->FindComponent<NetBindComponent>()->SetOwningConnectionId(AzNetworking::ConnectionId{ 123 });
        EXPECT_EQ(
            static_cast<OwnerTrackingScriptDefinedComponent*>(entity->FindComponent<MultiplayerScriptDefinedComponent>())
                ->m_lastOwningConnection,
            AzNetworking::ConnectionId{ 123 });
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, OptionalInputRoundTripsCopiesAndReplays)
    {
        auto* entity = CreateEntity(NetEntityRole::Autonomous, true, true);
        auto* netBind = entity->FindComponent<NetBindComponent>();
        auto* component = &Observe(entity);
        EXPECT_EQ(netBind->AllocateComponentInputs().size(), 1);
        NetworkInputChild inputStorage;
        auto& input = inputStorage.GetNetworkInput();
        input.AttachNetBindComponent(netBind);
        netBind->CreateInput(input, 0.016f);
        ASSERT_EQ(component->m_createInputCalls, 1);
        AZStd::array<uint8_t, 512> buffer{};
        AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(input.Serialize(writer));
        NetworkInputChild decodedStorage;
        auto& decoded = decodedStorage.GetNetworkInput();
        decoded.AttachNetBindComponent(netBind);
        AzNetworking::NetworkOutputSerializer reader(buffer.data(), writer.GetSize());
        ASSERT_TRUE(decoded.Serialize(reader));
        NetworkInput historyCopy = decoded;
        netBind->ProcessInput(historyCopy, 0.016f);
        EXPECT_EQ(component->m_processedInput, component->m_createdInput);
        EXPECT_FALSE(component->m_wasReprocessing);
        netBind->ReprocessInput(historyCopy, 0.016f);
        EXPECT_EQ(component->m_processInputCalls, 2);
        EXPECT_TRUE(component->m_wasReprocessing);
        EXPECT_FALSE(Controller(entity)->IsInputEnabled());
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, InputDeltaRoundTripsAgainstSameLayout)
    {
        ScriptDefinedInputFrame base;
        base.m_values = { 0.25f, -0.5f };
        auto current = base;
        current.m_values[1] = 1.0f;
        AzNetworking::SerializerDelta delta;
        AzNetworking::DeltaSerializerCreate creator(delta);
        ASSERT_TRUE(creator.CreateDelta(base, current));
        EXPECT_EQ(delta.GetBufferSize(), sizeof(float));
        AzNetworking::DeltaSerializerApply apply(delta);
        auto decoded = base;
        ASSERT_TRUE(apply.ApplyDelta(decoded));
        EXPECT_EQ(decoded, current);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, WrongInputSchemaOrLayoutIsNotProcessed)
    {
        auto* entity = CreateEntity(NetEntityRole::Authority, true, true);
        auto* netBind = entity->FindComponent<NetBindComponent>();
        NetworkInputChild inputStorage;
        auto& input = inputStorage.GetNetworkInput();
        input.AttachNetBindComponent(netBind);
        auto* frame = input.FindComponentInput<MultiplayerScriptDefinedInputComponentNetworkInput>();
        ASSERT_NE(frame, nullptr);
        frame->m_frame.m_values = { 0.25f, -0.5f };
        frame->m_frame.m_schemaFingerprint = 0;
        netBind->ProcessInput(input, 0.016f);
        frame->m_frame.m_schemaFingerprint = TestSchemaFingerprint;
        frame->m_frame.m_values.clear();
        netBind->ProcessInput(input, 0.016f);
        EXPECT_EQ(Observe(entity).m_processInputCalls, 0);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, StructuredRpcUsesGeneratedEndpointAndNetworkEnvelope)
    {
        auto* authority = CreateEntity(NetEntityRole::Authority);
        auto* client = CreateEntity(NetEntityRole::Client);
        AZStd::vector<NetworkEntityRpcMessage> sent;
        AZ::Event<NetworkEntityRpcMessage&>::Handler handler(
            [&sent](NetworkEntityRpcMessage& message)
            {
                sent.push_back(message);
            });
        handler.Connect(authority->FindComponent<NetBindComponent>()->GetSendAuthorityToClientRpcEvent());
        Controller(authority)->Dispatch(MakeRpc());
        auto second = MakeRpc();
        second.m_rpcId = 2;
        second.m_arguments = { true };
        Controller(authority)->Dispatch(second);
        ASSERT_EQ(sent.size(), 2);
        for (auto& message : sent)
        {
            AZStd::array<uint8_t, 512> buffer{};
            AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
            ASSERT_TRUE(message.Serialize(writer));
            NetworkEntityRpcMessage decoded;
            AzNetworking::NetworkOutputSerializer reader(buffer.data(), writer.GetSize());
            ASSERT_TRUE(decoded.Serialize(reader));
            EXPECT_EQ(decoded.GetComponentId(), client->FindComponent<MultiplayerScriptDefinedComponent>()->GetNetComponentId());
            EXPECT_EQ(decoded.GetReliability(), ReliabilityType::Reliable);
            EXPECT_TRUE(
                client->FindComponent<MultiplayerScriptDefinedComponent>()->HandleRpcMessage(nullptr, NetEntityRole::Authority, decoded));
            const auto& received = Observe(client).m_lastRpc;
            if (received.m_rpcId == 1)
            {
                EXPECT_EQ(AZStd::get<int32_t>(received.m_arguments[0]), 42);
                EXPECT_FLOAT_EQ(AZStd::get<float>(received.m_arguments[1]), 0.5f);
                EXPECT_TRUE(AZStd::get<AZ::Vector3>(received.m_arguments[2]).IsClose(AZ::Vector3(1.0f, 2.0f, 3.0f)));
            }
        }
        EXPECT_EQ(Observe(client).m_receivedRpcCount, 2);
        EXPECT_EQ(Observe(client).m_lastRpc.m_rpcId, 2);
        EXPECT_TRUE(AZStd::get<bool>(Observe(client).m_lastRpc.m_arguments[0]));
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, WrongRpcRoleSchemaIdAndSignatureAreNotDispatched)
    {
        auto* authority = CreateEntity(NetEntityRole::Authority);
        auto* client = CreateEntity(NetEntityRole::Client);
        NetworkEntityRpcMessage sent;
        AZ::Event<NetworkEntityRpcMessage&>::Handler handler(
            [&sent](NetworkEntityRpcMessage& message)
            {
                sent = message;
            });
        handler.Connect(authority->FindComponent<NetBindComponent>()->GetSendAuthorityToClientRpcEvent());
        Controller(authority)->Dispatch(MakeRpc());
        auto* receiver = client->FindComponent<MultiplayerScriptDefinedComponent>();
        receiver->HandleRpcMessage(nullptr, NetEntityRole::Autonomous, sent);
        auto payload = MakeRpc();
        payload.m_schemaFingerprint = 0;
        Controller(authority)->Dispatch(payload);
        receiver->HandleRpcMessage(nullptr, NetEntityRole::Authority, sent);
        payload = MakeRpc();
        payload.m_rpcId = 99;
        Controller(authority)->Dispatch(payload);
        receiver->HandleRpcMessage(nullptr, NetEntityRole::Authority, sent);
        payload = MakeRpc();
        payload.m_arguments[1] = int32_t{ 1 };
        Controller(authority)->Dispatch(payload);
        receiver->HandleRpcMessage(nullptr, NetEntityRole::Authority, sent);
        payload = MakeRpc();
        payload.m_arguments.pop_back();
        Controller(authority)->Dispatch(payload);
        receiver->HandleRpcMessage(nullptr, NetEntityRole::Authority, sent);
        EXPECT_EQ(Observe(client).m_receivedRpcCount, 0);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, RpcEndpointWorksWithSerializerTypeValidation)
    {
        m_console->PerformCommand("net_validateSerializedTypes true");
        auto* authority = CreateEntity(NetEntityRole::Authority);
        auto* client = CreateEntity(NetEntityRole::Client);
        NetworkEntityRpcMessage sent;
        AZ::Event<NetworkEntityRpcMessage&>::Handler handler(
            [&sent](NetworkEntityRpcMessage& message)
            {
                sent = message;
            });
        handler.Connect(authority->FindComponent<NetBindComponent>()->GetSendAuthorityToClientRpcEvent());
        Controller(authority)->Dispatch(MakeRpc());
        auto* receiver = client->FindComponent<MultiplayerScriptDefinedComponent>();
        ASSERT_TRUE(receiver->HandleRpcMessage(nullptr, NetEntityRole::Authority, sent));
        EXPECT_EQ(Observe(client).m_receivedRpcCount, 1);
        EXPECT_EQ(AZStd::get<int32_t>(Observe(client).m_lastRpc.m_arguments[0]), 42);
        m_console->PerformCommand("net_validateSerializedTypes false");
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, WrongSenderRoleDoesNotEmitRpc)
    {
        auto* autonomous = CreateEntity(NetEntityRole::Autonomous);
        uint32_t sends = 0;
        AZ::Event<NetworkEntityRpcMessage&>::Handler handler(
            [&sends](NetworkEntityRpcMessage&)
            {
                ++sends;
            });
        handler.Connect(autonomous->FindComponent<NetBindComponent>()->GetSendAuthorityToClientRpcEvent());
        Controller(autonomous)->Dispatch(MakeRpc());
        EXPECT_EQ(sends, 0);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, RpcSerializationFailureDoesNotEmitSendEvent)
    {
        auto* authority = CreateEntity(NetEntityRole::Authority);
        uint32_t sends = 0;
        AZ::Event<NetworkEntityRpcMessage&>::Handler handler(
            [&sends](NetworkEntityRpcMessage&)
            {
                ++sends;
            });
        handler.Connect(authority->FindComponent<NetBindComponent>()->GetSendAuthorityToClientRpcEvent());
        auto payload = MakeRpc();
        payload.m_arguments[1] = std::numeric_limits<float>::infinity();
        Controller(authority)->Dispatch(payload);
        EXPECT_EQ(sends, 0);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, TruncatedRpcLeavesDestinationUnchanged)
    {
        auto payload = MakeRpc();
        AZStd::array<uint8_t, 512> buffer{};
        AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(payload.Serialize(writer));
        for (uint32_t length = 0; length < writer.GetSize(); ++length)
        {
            SCOPED_TRACE(length);
            auto decoded = MakeRpc();
            decoded.m_rpcId = 77;
            AzNetworking::NetworkOutputSerializer reader(buffer.data(), length);
            EXPECT_FALSE(decoded.Serialize(reader));
            EXPECT_FALSE(reader.IsValid());
            EXPECT_EQ(decoded.m_rpcId, 77);
            EXPECT_EQ(decoded.m_arguments.size(), 3);
            EXPECT_EQ(AZStd::get<int32_t>(decoded.m_arguments[0]), 42);
        }
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, ExcessiveCountsAndUnknownTypeTagAreRejectedBeforeResize)
    {
        AZStd::array<uint8_t, 512> buffer{};
        AZ::u64 schema = TestSchemaFingerprint;
        uint16_t rpc = 1;
        uint8_t count = ScriptDefinedArgumentCapacity + 1;
        AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        AzNetworking::ISerializer& fields = writer;
        ASSERT_TRUE(fields.Serialize(schema, "Schema"));
        ASSERT_TRUE(fields.Serialize(rpc, "Rpc"));
        ASSERT_TRUE(fields.Serialize(count, "Count"));
        auto payload = MakeRpc();
        AzNetworking::NetworkOutputSerializer reader(buffer.data(), writer.GetSize());
        EXPECT_FALSE(payload.Serialize(reader));
        EXPECT_FALSE(reader.IsValid());
        EXPECT_EQ(payload.m_arguments.size(), 3);

        count = 1;
        uint8_t tag = 255;
        AzNetworking::NetworkInputSerializer tagWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        AzNetworking::ISerializer& tagFields = tagWriter;
        ASSERT_TRUE(tagFields.Serialize(schema, "Schema"));
        ASSERT_TRUE(tagFields.Serialize(rpc, "Rpc"));
        ASSERT_TRUE(tagFields.Serialize(count, "Count"));
        ASSERT_TRUE(tagFields.Serialize(tag, "Type"));
        AzNetworking::NetworkOutputSerializer tagReader(buffer.data(), tagWriter.GetSize());
        EXPECT_FALSE(payload.Serialize(tagReader));
        EXPECT_FALSE(tagReader.IsValid());

        count = ScriptDefinedInputCapacity + 1;
        AzNetworking::NetworkInputSerializer inputWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        AzNetworking::ISerializer& inputFields = inputWriter;
        bool typed = false;
        ASSERT_TRUE(inputFields.Serialize(typed, "Typed"));
        ASSERT_TRUE(inputFields.Serialize(schema, "Schema"));
        ASSERT_TRUE(inputFields.Serialize(count, "Count"));
        ScriptDefinedInputFrame frame;
        frame.m_values = { 1.0f };
        AzNetworking::NetworkOutputSerializer inputReader(buffer.data(), inputWriter.GetSize());
        EXPECT_FALSE(frame.Serialize(inputReader));
        EXPECT_FALSE(inputReader.IsValid());
        EXPECT_EQ(frame.m_values.size(), 1);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, InputTruncationAndNonFiniteWireValuesLeaveDestinationUnchanged)
    {
        ScriptDefinedInputFrame source;
        source.m_values = { 0.25f, -0.5f };
        AZStd::array<uint8_t, 512> buffer{};
        AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(source.Serialize(writer));
        for (uint32_t length = 0; length < writer.GetSize(); ++length)
        {
            SCOPED_TRACE(length);
            ScriptDefinedInputFrame decoded;
            decoded.m_values = { 9.0f };
            AzNetworking::NetworkOutputSerializer reader(buffer.data(), length);
            EXPECT_FALSE(decoded.Serialize(reader));
            EXPECT_FALSE(reader.IsValid());
            ASSERT_EQ(decoded.m_values.size(), 1);
            EXPECT_FLOAT_EQ(decoded.m_values[0], 9.0f);
        }
        AzNetworking::NetworkInputSerializer invalidWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        AzNetworking::ISerializer& fields = invalidWriter;
        AZ::u64 schema = TestSchemaFingerprint;
        uint8_t count = 1;
        float value = std::numeric_limits<float>::infinity();
        bool typed = false;
        ASSERT_TRUE(fields.Serialize(typed, "Typed"));
        ASSERT_TRUE(fields.Serialize(schema, "Schema"));
        ASSERT_TRUE(fields.Serialize(count, "Count"));
        ASSERT_TRUE(fields.Serialize(value, "Value"));
        ScriptDefinedInputFrame decoded;
        decoded.m_values = { 9.0f };
        AzNetworking::NetworkOutputSerializer reader(buffer.data(), invalidWriter.GetSize());
        EXPECT_FALSE(decoded.Serialize(reader));
        EXPECT_FALSE(reader.IsValid());
        EXPECT_FLOAT_EQ(decoded.m_values[0], 9.0f);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, MaximumTransportRecordsFitAndRoundTrip)
    {
        ScriptDefinedRpcPayload payload;
        payload.m_arguments.resize(ScriptDefinedArgumentCapacity, AZ::Vector3(1.0f, 2.0f, 3.0f));
        AZStd::array<uint8_t, 512> buffer{};
        AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(payload.Serialize(writer));
        EXPECT_EQ(writer.GetSize(), 11 + ScriptDefinedArgumentCapacity * 13);
        ScriptDefinedRpcPayload decoded;
        AzNetworking::NetworkOutputSerializer reader(buffer.data(), writer.GetSize());
        ASSERT_TRUE(decoded.Serialize(reader));
        ASSERT_EQ(decoded.m_arguments.size(), ScriptDefinedArgumentCapacity);
        for (const auto& argument : decoded.m_arguments)
        {
            EXPECT_TRUE(AZStd::get<AZ::Vector3>(argument).IsClose(AZ::Vector3(1.0f, 2.0f, 3.0f)));
        }
        ScriptDefinedInputFrame frame;
        frame.m_values.resize(ScriptDefinedInputCapacity, 1.0f);
        AzNetworking::NetworkInputSerializer inputWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(frame.Serialize(inputWriter));
        EXPECT_EQ(inputWriter.GetSize(), 10 + ScriptDefinedInputCapacity * sizeof(float));
        ScriptDefinedInputFrame decodedFrame;
        AzNetworking::NetworkOutputSerializer inputReader(buffer.data(), inputWriter.GetSize());
        ASSERT_TRUE(decodedFrame.Serialize(inputReader));
        EXPECT_EQ(decodedFrame, frame);
    }

    TEST_F(MultiplayerScriptDefinedComponentTests, Measurements)
    {
        std::printf("\nMULTIPLAYER_SCRIPT_DEFINED memory_bytes (sizeof; excludes allocator overhead)\n");
        std::printf("capacity,fixed_float_bank,rewindable_float_bank,dirty_bits\n");
        std::printf("4,%zu,%zu,5\n", sizeof(AZStd::fixed_vector<float, 4>), sizeof(RewindableFixedVector<float, 4>));
        std::printf("16,%zu,%zu,17\n", sizeof(AZStd::fixed_vector<float, 16>), sizeof(RewindableFixedVector<float, 16>));
        std::printf("64,%zu,%zu,65\n", sizeof(AZStd::fixed_vector<float, 64>), sizeof(RewindableFixedVector<float, 64>));
        std::printf(
            "generated_state_component,%zu\ngenerated_state_controller,%zu\ngenerated_component_record,%zu\n",
            sizeof(MultiplayerScriptDefinedComponentBase),
            sizeof(MultiplayerScriptDefinedComponentControllerBase),
            sizeof(MultiplayerScriptDefinedComponentRecord));
        std::printf(
            "runtime_state_component,%zu\nruntime_input_component,%zu\nrpc_signature,%zu\n",
            sizeof(MultiplayerScriptDefinedComponent),
            sizeof(MultiplayerScriptDefinedInputComponent),
            sizeof(ScriptDefinedRpcSignature));
        std::printf(
            "generated_input_component,%zu\ngenerated_input_controller,%zu\nallocated_input_frame,%zu\n",
            sizeof(MultiplayerScriptDefinedInputComponentBase),
            sizeof(MultiplayerScriptDefinedInputComponentControllerBase),
            sizeof(MultiplayerScriptDefinedInputComponentNetworkInput));
        std::printf(
            "rpc_value,%zu\nrpc_payload,%zu\nreplication_record,%zu\nrewind_frames,%u\n",
            sizeof(ScriptDefinedRpcValue),
            sizeof(ScriptDefinedRpcPayload),
            sizeof(ReplicationRecord),
            RewindHistorySize);
        for (size_t count : { size_t{ 0 }, size_t{ 1 }, size_t{ ScriptDefinedInputCapacity } })
        {
            MultiplayerScriptDefinedInputComponentNetworkInput typedInput;
            typedInput.m_frame.m_isTyped = true;
            typedInput.m_frame.m_typedValues.resize(count, ScriptDefinedRpcValue(false));
            std::printf(
                "typed_input_storage,slots=%zu,frame_bytes=%zu,vector_heap_bytes=%zu\n",
                count,
                sizeof(typedInput),
                typedInput.m_frame.m_typedValues.capacity() * sizeof(ScriptDefinedRpcValue));
        }

        auto* authority = CreateEntity(NetEntityRole::Authority);
        auto* controller = Controller(authority);
        AZStd::array<uint8_t, 2048> buffer{};
        for (uint32_t count : { 0U, 1U, 4U, 16U })
        {
            while (controller->ValuesGetSize() < count)
            {
                ASSERT_TRUE(controller->ValuesPushBack(1.0f));
            }
            auto dirty = ChangedRecord(authority);
            AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
            auto record = dirty;
            ASSERT_TRUE(record.Serialize(writer));
            const auto recordBytes = writer.GetSize();
            record.ResetConsumedBits();
            ASSERT_TRUE(authority->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(record, writer));
            std::printf(
                "bank_growth,slots=%u,reserved_bits=%u,set_bits=%u,record_bytes=%u,value_bytes=%u\n",
                count,
                dirty.m_authorityToClient.GetValidBitCount(),
                CountDirty(dirty.m_authorityToClient),
                recordBytes,
                writer.GetSize() - recordBytes);
            m_networkEntityManager->NotifyEntitiesDirtied();
        }
        ReplicationRecord snapshot(NetEntityRole::Client);
        authority->FindComponent<NetBindComponent>()->FillTotalReplicationRecord(snapshot);
        AzNetworking::NetworkInputSerializer snapshotWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(snapshot.Serialize(snapshotWriter));
        const auto snapshotRecordBytes = snapshotWriter.GetSize();
        auto snapshotForWrite = snapshot;
        ASSERT_TRUE(authority->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(snapshotForWrite, snapshotWriter));
        std::printf(
            "bank_snapshot,slots=16,set_bits=%u,record_bytes=%u,value_bytes=%u\n",
            CountDirty(snapshot.m_authorityToClient),
            snapshotRecordBytes,
            snapshotWriter.GetSize() - snapshotRecordBytes);
        ASSERT_TRUE(controller->TrySetValue(7, 2.0f));
        auto dirty = ChangedRecord(authority);
        AzNetworking::NetworkInputSerializer deltaWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        auto record = dirty;
        ASSERT_TRUE(record.Serialize(deltaWriter));
        const auto recordBytes = deltaWriter.GetSize();
        record.ResetConsumedBits();
        ASSERT_TRUE(authority->FindComponent<NetBindComponent>()->SerializeStateDeltaMessage(record, deltaWriter));
        std::printf(
            "bank_delta,slots=16,set_bits=%u,record_bytes=%u,value_bytes=%u\n",
            CountDirty(dirty.m_authorityToClient),
            recordBytes,
            deltaWriter.GetSize() - recordBytes);

        for (bool enabled : { false, true })
        {
            auto* entity = CreateEntity(NetEntityRole::Autonomous, true, enabled);
            NetworkInputChild inputStorage;
            auto& input = inputStorage.GetNetworkInput();
            input.AttachNetBindComponent(entity->FindComponent<NetBindComponent>());
            entity->FindComponent<NetBindComponent>()->CreateInput(input, 0.016f);
            AzNetworking::NetworkInputSerializer writer(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
            ASSERT_TRUE(input.Serialize(writer));
            std::printf(
                "network_input,enabled=%u,component_frames=%zu,bytes=%u\n",
                enabled ? 1 : 0,
                entity->FindComponent<NetBindComponent>()->AllocateComponentInputs().size(),
                writer.GetSize());
        }
        ScriptDefinedInputFrame baseInput;
        baseInput.m_values = { 0.25f, -0.5f };
        auto currentInput = baseInput;
        currentInput.m_values[1] = 1.0f;
        AzNetworking::SerializerDelta inputDelta;
        AzNetworking::DeltaSerializerCreate inputDeltaCreator(inputDelta);
        ASSERT_TRUE(inputDeltaCreator.CreateDelta(baseInput, currentInput));
        AzNetworking::NetworkInputSerializer inputDeltaWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(inputDelta.Serialize(inputDeltaWriter));
        std::printf(
            "input_delta,slots=2,field_bits=%u,value_buffer_bytes=%u,serialized_bytes=%u\n",
            inputDelta.GetNumDirtyBits(),
            inputDelta.GetBufferSize(),
            inputDeltaWriter.GetSize());
        auto payload = MakeRpc();
        AzNetworking::NetworkInputSerializer payloadWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(payload.Serialize(payloadWriter));
        NetworkEntityRpcMessage message;
        AZ::Event<NetworkEntityRpcMessage&>::Handler handler(
            [&message](NetworkEntityRpcMessage& sent)
            {
                message = sent;
            });
        handler.Connect(authority->FindComponent<NetBindComponent>()->GetSendAuthorityToClientRpcEvent());
        controller->Dispatch(payload);
        AzNetworking::NetworkInputSerializer rpcWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
        ASSERT_TRUE(message.Serialize(rpcWriter));
        std::printf("rpc,arguments=3,payload_bytes=%u,envelope_and_payload_bytes=%u\n", payloadWriter.GetSize(), rpcWriter.GetSize());
        payload.m_arguments.resize(ScriptDefinedArgumentCapacity);
        ScriptDefinedString fullString;
        fullString.resize(fullString.max_size(), 'x');
        for (bool strings : { false, true })
        {
            for (auto& argument : payload.m_arguments)
            {
                argument = strings ? ScriptDefinedRpcValue(fullString) : ScriptDefinedRpcValue(AZ::Vector3(1.0f, 2.0f, 3.0f));
            }
            AzNetworking::NetworkInputSerializer boundWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
            ASSERT_TRUE(payload.Serialize(boundWriter));
            controller->Dispatch(payload);
            AzNetworking::NetworkInputSerializer boundRpcWriter(buffer.data(), aznumeric_cast<uint32_t>(buffer.size()));
            ASSERT_TRUE(message.Serialize(boundRpcWriter));
            std::printf(
                "rpc_bound,type=%s,arguments=8,payload_bytes=%u,envelope_and_payload_bytes=%u\n",
                strings ? "string64" : "vector3",
                boundWriter.GetSize(),
                boundRpcWriter.GetSize());
        }
    }
} // namespace Multiplayer
