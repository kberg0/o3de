/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/std/any.h>
#include <Multiplayer/MultiplayerScriptDefinedDefinition.h>
#include <ScriptEvents/ScriptEventDefinition.h>

namespace ScriptCanvasMultiplayer
{
    //! @{
    //! Names of the generated Script Events notifications. Graphs refer to these events by name.
    inline constexpr const char* ScriptDefinedPropertyChangedPrefix = "On_";
    inline constexpr const char* ScriptDefinedRpcReceivedPrefix = "OnRpc_";
    inline constexpr const char* ScriptDefinedCreateInputEvent = "OnCreateInput";
    inline constexpr const char* ScriptDefinedProcessInputEvent = "OnProcessInput";
    //! @}

    //! Exact signed 64-bit integer pin. Script Canvas's Number type cannot represent all 64-bit integers.
    //! An invalid value results from a failed parse or an overflow, and stays invalid through arithmetic.
    struct ScriptDefinedInt64
    {
        AZ_TYPE_INFO(ScriptDefinedInt64, "{8C2FB0DB-4923-4636-9F32-53A0EC5E4F1C}");

        //! Parses decimal text. The result is invalid unless the whole text is an in-range integer.
        static ScriptDefinedInt64 FromDecimal(const AZStd::string& text);

        //! Returns the decimal text of a valid value, or an empty string.
        AZStd::string ToDecimal() const;

        bool IsValid() const
        {
            return m_valid;
        }

        //! Returns the sum, which is invalid if either operand is invalid or the sum overflows.
        ScriptDefinedInt64 Add(ScriptDefinedInt64 other) const;

        //! @{
        //! Compare two values. Return false if either value is invalid.
        bool Equal(ScriptDefinedInt64 other) const
        {
            return m_valid && other.m_valid && m_value == other.m_value;
        }

        bool Less(ScriptDefinedInt64 other) const
        {
            return m_valid && other.m_valid && m_value < other.m_value;
        }
        //! @}

        AZ::s64 m_value = 0;
        bool m_valid = true;
    };

    //! Exact unsigned 64-bit integer pin. Behaves like ScriptDefinedInt64.
    struct ScriptDefinedUInt64
    {
        AZ_TYPE_INFO(ScriptDefinedUInt64, "{A9039123-B6A0-4CBE-BC19-071DFB35D750}");

        //! Parses decimal text. The result is invalid unless the whole text is an in-range integer.
        static ScriptDefinedUInt64 FromDecimal(const AZStd::string& text);

        //! Returns the decimal text of a valid value, or an empty string.
        AZStd::string ToDecimal() const;

        bool IsValid() const
        {
            return m_valid;
        }

        //! Returns the sum, which is invalid if either operand is invalid or the sum overflows.
        ScriptDefinedUInt64 Add(ScriptDefinedUInt64 other) const;

        //! @{
        //! Compare two values. Return false if either value is invalid.
        bool Equal(ScriptDefinedUInt64 other) const
        {
            return m_valid && other.m_valid && m_value == other.m_value;
        }

        bool Less(ScriptDefinedUInt64 other) const
        {
            return m_valid && other.m_valid && m_value < other.m_value;
        }
        //! @}

        AZ::u64 m_value = 0;
        bool m_valid = true;
    };

    //! Reflects ScriptDefinedInt64 and ScriptDefinedUInt64 to serialization and to scripts.
    void ReflectScriptDefinedIntegers(AZ::ReflectContext* context);

    //! What a generated Script Events method does when a graph calls it.
    enum class ScriptDefinedOperation
    {
        GetProperty,
        SetProperty,
        GetInput,
        SetInput,
        GetArchetype,
        SendRpc,
        Notification
    };

    //! One method of the generated Script Events interface.
    struct ScriptDefinedMethod
    {
        ScriptDefinedOperation m_operation;

        //! ID of the schema member the method reads, writes or sends.
        AZ::Uuid m_memberId;

        AZStd::string m_name;
        AZStd::vector<Multiplayer::ScriptDefinedParameter> m_parameters;
        AZ::Uuid m_returnType = azrtti_typeid<void>();
    };

    //! Returns the Script Canvas pin type for a value type: the type that ToScriptPinValue converts its values to.
    AZ::Uuid ScriptDefinedPinType(Multiplayer::ScriptDefinedRpcType type);

    //! Converts a value to its Script Canvas pin type. 64-bit integers use the exact integer types, other numbers use Number
    //! (double), strings use AZStd::string and net entities use the local entity ID, invalid when the entity is not present.
    AZStd::any ToScriptPinValue(const Multiplayer::ScriptDefinedRpcValue& value);

    //! Lists the methods of the interface generated for a definition, in a stable order.
    AZStd::vector<ScriptDefinedMethod> MakeScriptDefinedMethods(const Multiplayer::MultiplayerScriptDefinedDefinition& definition);

    //! Builds the generated, read-only Script Events definition for a schema. Its version is the schema revision,
    //! and its member IDs derive from schema member IDs, so regenerated interfaces keep their graph connections.
    ScriptEvents::ScriptEvent MakeScriptDefinedInterface(const Multiplayer::MultiplayerScriptDefinedDefinition& definition);

    //! Converts a Script Canvas argument to a value of the given type.
    //! Returns false if the argument has the wrong type, is out of range, or is not finite.
    bool ReadScriptDefinedArgument(
        Multiplayer::ScriptDefinedRpcType type, AZ::BehaviorArgument& argument, Multiplayer::ScriptDefinedRpcValue& value);

    //! Stores a value into a Script Canvas result or argument, converted to its pin type.
    bool StoreScriptDefinedResult(const Multiplayer::ScriptDefinedRpcValue& value, AZ::BehaviorArgument& result);
} // namespace ScriptCanvasMultiplayer
