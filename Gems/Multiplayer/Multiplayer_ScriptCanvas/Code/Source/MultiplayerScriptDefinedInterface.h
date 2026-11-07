/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once

#include <AzCore/RTTI/BehaviorContext.h>
#include <Multiplayer/MultiplayerScriptDefinedDefinition.h>
#include <ScriptEvents/ScriptEventDefinition.h>

namespace ScriptCanvasMultiplayer
{
    //! Exact integer pins; Script Canvas's Number type cannot represent all 64-bit integers.
    struct ScriptDefinedInt64
    {
        AZ_TYPE_INFO(ScriptDefinedInt64, "{8C2FB0DB-4923-4636-9F32-53A0EC5E4F1C}");
        AZ::s64 m_value = 0;
        bool m_valid = true;
        static ScriptDefinedInt64 FromDecimal(const AZStd::string& text);
        AZStd::string ToDecimal() const;
        bool IsValid() const
        {
            return m_valid;
        }
        ScriptDefinedInt64 Add(ScriptDefinedInt64 other) const;
        bool Equal(ScriptDefinedInt64 other) const
        {
            return m_valid && other.m_valid && m_value == other.m_value;
        }
        bool Less(ScriptDefinedInt64 other) const
        {
            return m_valid && other.m_valid && m_value < other.m_value;
        }
    };
    struct ScriptDefinedUInt64
    {
        AZ_TYPE_INFO(ScriptDefinedUInt64, "{A9039123-B6A0-4CBE-BC19-071DFB35D750}");
        AZ::u64 m_value = 0;
        bool m_valid = true;
        static ScriptDefinedUInt64 FromDecimal(const AZStd::string& text);
        AZStd::string ToDecimal() const;
        bool IsValid() const
        {
            return m_valid;
        }
        ScriptDefinedUInt64 Add(ScriptDefinedUInt64 other) const;
        bool Equal(ScriptDefinedUInt64 other) const
        {
            return m_valid && other.m_valid && m_value == other.m_value;
        }
        bool Less(ScriptDefinedUInt64 other) const
        {
            return m_valid && other.m_valid && m_value < other.m_value;
        }
    };
    void ReflectScriptDefinedIntegers(AZ::ReflectContext* context);

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
    struct ScriptDefinedMethod
    {
        ScriptDefinedOperation m_operation;
        AZ::Uuid m_memberId;
        AZStd::string m_name;
        AZStd::vector<Multiplayer::ScriptDefinedParameter> m_parameters;
        AZ::Uuid m_returnType = azrtti_typeid<void>();
    };
    AZ::Uuid ScriptDefinedPinType(Multiplayer::ScriptDefinedRpcType type);
    AZStd::vector<ScriptDefinedMethod> MakeScriptDefinedMethods(const Multiplayer::MultiplayerScriptDefinedDefinition& definition);
    ScriptEvents::ScriptEvent MakeScriptDefinedInterface(const Multiplayer::MultiplayerScriptDefinedDefinition& definition);
    bool ReadScriptDefinedArgument(
        Multiplayer::ScriptDefinedRpcType type, AZ::BehaviorArgument& argument, Multiplayer::ScriptDefinedRpcValue& value);
    bool StoreScriptDefinedResult(const Multiplayer::ScriptDefinedRpcValue& value, AZ::BehaviorArgument& result);
} // namespace ScriptCanvasMultiplayer
