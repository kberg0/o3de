/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/EBus/EBus.h>
#include <AzCore/Outcome/Outcome.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>
#include <Multiplayer/MultiplayerScriptDefinedTypes.h>

namespace Multiplayer
{
    //! Sub ID of the Script Events interface product that the schema builder generates next to the schema product.
    inline constexpr uint32_t ScriptDefinedInterfaceSubId = 1;

    //! Maximum length of a schema, member or parameter name.
    inline constexpr size_t ScriptDefinedIdentifierCapacity = 64;

    //! Maximum number of archetype fields in one schema.
    inline constexpr size_t ScriptDefinedArchetypeCapacity = 256;

    //! A named, typed RPC parameter.
    struct ScriptDefinedParameter
    {
        AZ_TYPE_INFO(ScriptDefinedParameter, "{E37923D3-842F-4434-9317-31BC0FD5E739}");
        static void Reflect(AZ::ReflectContext* context);

        //! Label shown for this parameter in property editors.
        AZStd::string GetLabel() const
        {
            return m_name;
        }

        AZ::Uuid m_id = AZ::Uuid::CreateRandom();
        AZStd::string m_name = "Field";
        ScriptDefinedRpcType m_type = ScriptDefinedRpcType::Float;
    };

    //! A named, typed property, input or archetype field with its default value.
    struct ScriptDefinedField : ScriptDefinedParameter
    {
        AZ_TYPE_INFO(ScriptDefinedField, "{2B52CF1B-C53F-4078-9CDD-D2A078A49163}");
        static void Reflect(AZ::ReflectContext* context);

        //! Label shown for this field in property editors.
        AZStd::string GetLabel() const
        {
            return m_name;
        }

        //! Default in the text form ParseScriptDefinedValue accepts. Empty uses the type's default.
        AZStd::string m_defaultValue;

        //! Allows autonomous writes during input prediction and adds the property to prediction records, so the autonomous
        //! peer is corrected when it mispredicts. Without it the property is authority-only. Only meaningful for properties.
        bool m_predictable = false;

        //! Keeps a rewind history for the property, like a native IsRewindable network property: while the server rewinds
        //! time for lag compensation, reads return the value of the rewound frame. Without it reads always return the
        //! current value. Only meaningful for properties; at most ScriptDefinedRewindableCapacity properties are rewindable.
        bool m_rewindable = false;
    };

    //! A named RPC with its delivery route and typed parameters.
    struct ScriptDefinedRpcDefinition
    {
        AZ_TYPE_INFO(ScriptDefinedRpcDefinition, "{E5D8B7C1-A384-4F89-AFBC-2750D6DBBF09}");
        static void Reflect(AZ::ReflectContext* context);

        //! Label shown for this RPC in property editors.
        AZStd::string GetLabel() const
        {
            return m_name;
        }

        AZ::Uuid m_id = AZ::Uuid::CreateRandom();
        AZStd::string m_name = "Rpc";
        RpcDeliveryType m_delivery = RpcDeliveryType::AuthorityToClient;
        bool m_isReliable = true;
        AZStd::vector<ScriptDefinedParameter> m_parameters;
    };

    //! An authored multiplayer schema: replicated properties, optional input fields, read-only archetype
    //! configuration and RPCs. Wire IDs and property slots are derived from member IDs, so they do not
    //! depend on the order in which members are authored.
    struct MultiplayerScriptDefinedDefinition
    {
        AZ_TYPE_INFO(MultiplayerScriptDefinedDefinition, "{14ED7206-C56D-4C66-8189-E4599B07D878}");
        static void Reflect(AZ::ReflectContext* context);

        //! Checks names, IDs, types, defaults and capacities. Returns a description of the first problem found.
        AZ::Outcome<void, AZStd::string> Validate() const;

        //! Returns a nonzero hash of everything that affects the wire format. Peers only exchange data when their fingerprints match.
        AZ::u64 GetFingerprint() const;

        //! Returns the wire signature of every RPC.
        AZStd::vector<ScriptDefinedRpcSignature> GetRpcSignatures() const;

        //! Returns the wire ID of an RPC, or 0 if no RPC has that member ID.
        uint16_t GetRpcId(const AZ::Uuid& id) const;

        //! Returns the index of a property within its bank: the rewindable bank for rewindable properties, otherwise the bank
        //! of its type. Returns ScriptDefinedPropertyCapacity if the ID is not a property.
        uint32_t GetPropertySlot(const AZ::Uuid& id) const;

        //! @{
        //! Find a member by ID. Return nullptr if there is none.
        const ScriptDefinedField* FindProperty(const AZ::Uuid& id) const;
        const ScriptDefinedField* FindArchetype(const AZ::Uuid& id) const;
        const ScriptDefinedRpcDefinition* FindRpc(const AZ::Uuid& id) const;
        //! @}

        AZ::Uuid m_id = AZ::Uuid::CreateRandom();
        AZStd::string m_name = "MultiplayerComponent";
        AZ::u32 m_revision = 1;
        AZStd::vector<ScriptDefinedField> m_properties;
        AZStd::vector<ScriptDefinedField> m_inputs;
        AZStd::vector<ScriptDefinedField> m_archetypes;
        AZStd::vector<ScriptDefinedRpcDefinition> m_rpcs;
    };

    //! Source asset (.mpschema) holding one definition.
    class MultiplayerScriptDefinedAsset : public AZ::Data::AssetData
    {
    public:
        AZ_RTTI(MultiplayerScriptDefinedAsset, "{3F4C9B2E-1E51-4641-B5AB-DC1632718C9A}", AZ::Data::AssetData);
        AZ_CLASS_ALLOCATOR(MultiplayerScriptDefinedAsset, AZ::SystemAllocator);

        static void Reflect(AZ::ReflectContext* context);

        explicit MultiplayerScriptDefinedAsset(const AZ::Data::AssetId& id = {}, AssetStatus status = AssetStatus::NotLoaded)
            : AZ::Data::AssetData(id, status)
        {
        }

        //! @{
        //! Access the definition. Editing it does not refresh the validation text; ValidateForEditor does.
        const MultiplayerScriptDefinedDefinition& GetDefinition() const;
        MultiplayerScriptDefinedDefinition& ModifyDefinition();
        void SetDefinition(const MultiplayerScriptDefinedDefinition& definition);
        //! @}

        //! Refreshes the validation text shown in the Asset Editor and returns the property refresh level.
        AZ::u32 ValidateForEditor();

    private:
        MultiplayerScriptDefinedDefinition m_definition;
        AZStd::string m_validation = "Valid";
    };

    //! Returns the default value of a type: zero, false, identity, an empty string or an invalid entity.
    //! Returns a Boolean false for an unknown type.
    ScriptDefinedRpcValue DefaultScriptDefinedValue(ScriptDefinedRpcType type);

    //! Parses the text form of a value. Empty text gives the type's default. Integers are decimal and strings hold up to
    //! ScriptDefinedStringCapacity bytes. Math types are comma-separated finite components: x,y[,z[,w]] for vectors and
    //! quaternions, row-major elements for matrices, and translation x,y,z, rotation x,y,z,w, uniform scale for transforms.
    AZ::Outcome<ScriptDefinedRpcValue, AZStd::string> ParseScriptDefinedValue(ScriptDefinedRpcType type, AZStd::string_view text);

    //! Formats a value in the text form ParseScriptDefinedValue reads back exactly.
    AZStd::string FormatScriptDefinedValue(const ScriptDefinedRpcValue& value);

    //! Returns true if the name is a C identifier of at most ScriptDefinedIdentifierCapacity characters.
    bool IsScriptDefinedIdentifier(AZStd::string_view name);

    //! Requests to the script-defined component of an entity.
    class ScriptDefinedRequests : public AZ::EBusTraits
    {
    public:
        // EBusTraits overrides
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        using BusIdType = AZ::EntityId;

        //! Returns the definition the component was configured with, or nullptr if it has none.
        virtual const MultiplayerScriptDefinedDefinition* GetDefinition() const = 0;

        //! Reads the current value of a property. Returns false if the ID is not a property.
        virtual bool ReadProperty(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const = 0;

        //! Writes a property. Only the authority can write, or the autonomous peer for predictable properties.
        //! Returns false if the write is not allowed or the value has the wrong type or is not finite.
        virtual bool WriteProperty(const AZ::Uuid& id, const ScriptDefinedRpcValue& value) = 0;

        //! Reads the configured value of an archetype field. Returns false if the ID is not an archetype.
        virtual bool ReadArchetype(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const = 0;

        //! Sends an RPC. Returns false if the arguments do not match its signature or this peer cannot send it.
        virtual bool InvokeRpc(
            const AZ::Uuid& id, const AZStd::fixed_vector<ScriptDefinedRpcValue, ScriptDefinedArgumentCapacity>& arguments) = 0;
    };

    using ScriptDefinedRequestBus = AZ::EBus<ScriptDefinedRequests>;

    //! Requests to the script-defined input component of an entity.
    //! Inputs can only be read while input is processed and only be written while input is created.
    class ScriptDefinedInputRequests : public AZ::EBusTraits
    {
    public:
        // EBusTraits overrides
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        using BusIdType = AZ::EntityId;

        //! Reads an input field of the frame being processed. Returns false outside input processing.
        virtual bool ReadInput(const AZ::Uuid& id, ScriptDefinedRpcValue& value) const = 0;

        //! Writes an input field of the frame being created. Returns false outside input creation or on a type mismatch.
        virtual bool WriteInput(const AZ::Uuid& id, const ScriptDefinedRpcValue& value) = 0;
    };

    using ScriptDefinedInputRequestBus = AZ::EBus<ScriptDefinedInputRequests>;

    //! Notifications from the script-defined components of an entity.
    class ScriptDefinedNotifications : public AZ::EBusTraits
    {
    public:
        // EBusTraits overrides
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::ById;
        using BusIdType = AZ::EntityId;

        //! Sent when a property changes locally or through replication.
        virtual void OnPropertyChanged([[maybe_unused]] const AZ::Uuid& id, [[maybe_unused]] const ScriptDefinedRpcValue& value)
        {
        }

        //! Sent when an RPC that matches the schema is received.
        virtual void OnRpcReceived([[maybe_unused]] uint16_t rpcId, [[maybe_unused]] const ScriptDefinedRpcPayload& payload)
        {
        }

        //! Sent while an input frame is created; handlers write inputs during this call.
        virtual void OnCreateInput([[maybe_unused]] float deltaTime)
        {
        }

        //! Sent while an input frame is processed; handlers read inputs during this call.
        virtual void OnProcessInput([[maybe_unused]] float deltaTime, [[maybe_unused]] bool reprocessing)
        {
        }
    };

    using ScriptDefinedNotificationBus = AZ::EBus<ScriptDefinedNotifications>;
} // namespace Multiplayer
