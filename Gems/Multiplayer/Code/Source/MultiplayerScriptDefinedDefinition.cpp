/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Utils/TypeHash.h>
#include <AzCore/std/containers/unordered_set.h>
#include <AzCore/std/sort.h>
#include <Multiplayer/MultiplayerScriptDefinedDefinition.h>
#include <charconv>
#include <cmath>
#include <limits>

namespace Multiplayer
{
    namespace
    {
        template<class T>
        bool ParseNumber(AZStd::string_view text, T& value)
        {
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            return result.ec == std::errc{} && result.ptr == text.data() + text.size();
        }
        template<class T>
        AZStd::string FormatNumber(T value)
        {
            char buffer[64];
            auto end = buffer;
            if constexpr (AZStd::is_floating_point_v<T>)
            {
                end =
                    std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, std::numeric_limits<T>::max_digits10)
                        .ptr;
            }
            else
            {
                end = std::to_chars(buffer, buffer + sizeof(buffer), value).ptr;
            }
            return AZStd::string(buffer, end);
        }
        template<class Range>
        auto Sorted(const Range& fields)
        {
            using Element = typename Range::value_type;
            AZStd::vector<const Element*> result;
            for (const auto& field : fields)
            {
                result.push_back(&field);
            }
            AZStd::sort(
                result.begin(),
                result.end(),
                [](auto* a, auto* b)
                {
                    return a->m_id < b->m_id;
                });
            return result;
        }
        bool ValidRoute(RpcDeliveryType route)
        {
            return route == RpcDeliveryType::AuthorityToClient || route == RpcDeliveryType::AuthorityToAutonomous ||
                route == RpcDeliveryType::AutonomousToAuthority || route == RpcDeliveryType::ServerToAuthority;
        }
    } // namespace

    bool IsScriptDefinedIdentifier(AZStd::string_view name)
    {
        if (name.empty() || name.size() > 64)
        {
            return false;
        }
        auto letter = [](char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        };
        if (!letter(name[0]))
        {
            return false;
        }
        for (char c : name)
        {
            if (!letter(c) && !(c >= '0' && c <= '9'))
            {
                return false;
            }
        }
        return true;
    }

    ScriptDefinedRpcValue DefaultScriptDefinedValue(ScriptDefinedRpcType type)
    {
        switch (type)
        {
        case ScriptDefinedRpcType::Boolean:
            return ScriptDefinedRpcValue(AZStd::in_place_index<0>, false);
        case ScriptDefinedRpcType::Int32:
            return ScriptDefinedRpcValue(AZStd::in_place_index<1>, int32_t{ 0 });
        case ScriptDefinedRpcType::Float:
            return ScriptDefinedRpcValue(AZStd::in_place_index<2>, 0.0f);
        case ScriptDefinedRpcType::Vector3:
            return ScriptDefinedRpcValue(AZStd::in_place_index<3>, AZ::Vector3::CreateZero());
        case ScriptDefinedRpcType::Int8:
            return ScriptDefinedRpcValue(AZStd::in_place_index<4>, int8_t{ 0 });
        case ScriptDefinedRpcType::UInt8:
            return ScriptDefinedRpcValue(AZStd::in_place_index<5>, uint8_t{ 0 });
        case ScriptDefinedRpcType::Int16:
            return ScriptDefinedRpcValue(AZStd::in_place_index<6>, int16_t{ 0 });
        case ScriptDefinedRpcType::UInt16:
            return ScriptDefinedRpcValue(AZStd::in_place_index<7>, uint16_t{ 0 });
        case ScriptDefinedRpcType::UInt32:
            return ScriptDefinedRpcValue(AZStd::in_place_index<8>, uint32_t{ 0 });
        case ScriptDefinedRpcType::Int64:
            return ScriptDefinedRpcValue(AZStd::in_place_index<9>, int64_t{ 0 });
        case ScriptDefinedRpcType::UInt64:
            return ScriptDefinedRpcValue(AZStd::in_place_index<10>, uint64_t{ 0 });
        case ScriptDefinedRpcType::Double:
            return ScriptDefinedRpcValue(AZStd::in_place_index<11>, 0.0);
        case ScriptDefinedRpcType::Vector2:
            return ScriptDefinedRpcValue(AZStd::in_place_index<12>, AZ::Vector2::CreateZero());
        case ScriptDefinedRpcType::Quaternion:
            return ScriptDefinedRpcValue(AZStd::in_place_index<13>, AZ::Quaternion::CreateIdentity());
        case ScriptDefinedRpcType::String:
            return ScriptDefinedRpcValue(AZStd::in_place_index<14>, ScriptDefinedString{});
        case ScriptDefinedRpcType::Entity:
            return ScriptDefinedRpcValue(AZStd::in_place_index<15>, InvalidNetEntityId);
        default:
            return false;
        }
    }

    AZ::Outcome<ScriptDefinedRpcValue, AZStd::string> ParseScriptDefinedValue(ScriptDefinedRpcType type, AZStd::string_view text)
    {
        if (static_cast<unsigned>(type) > static_cast<unsigned>(ScriptDefinedRpcType::Entity))
        {
            return AZ::Failure(AZStd::string("Unknown value type"));
        }
        auto value = DefaultScriptDefinedValue(type);
        if (text.empty())
        {
            return AZ::Success(value);
        }
        bool valid = false;
        AZStd::visit(
            [&](auto& item)
            {
                using T = AZStd::decay_t<decltype(item)>;
                if constexpr (AZStd::is_same_v<T, bool>)
                {
                    valid = text == "true" || text == "false" || text == "1" || text == "0";
                    item = text == "true" || text == "1";
                }
                else if constexpr (AZStd::is_integral_v<T>)
                {
                    if constexpr (AZStd::is_signed_v<T>)
                    {
                        int64_t parsed = 0;
                        valid =
                            ParseNumber(text, parsed) && parsed >= std::numeric_limits<T>::min() && parsed <= std::numeric_limits<T>::max();
                        if (valid)
                        {
                            item = static_cast<T>(parsed);
                        }
                    }
                    else
                    {
                        uint64_t parsed = 0;
                        valid = ParseNumber(text, parsed) && parsed <= std::numeric_limits<T>::max();
                        if (valid)
                        {
                            item = static_cast<T>(parsed);
                        }
                    }
                }
                else if constexpr (AZStd::is_floating_point_v<T>)
                {
                    valid = ParseNumber(text, item) && std::isfinite(item);
                }
                else if constexpr (AZStd::is_same_v<T, ScriptDefinedString>)
                {
                    valid = text.size() <= item.max_size();
                    if (valid)
                    {
                        item.assign(text.data(), text.size());
                    }
                }
                else if constexpr (AZStd::is_same_v<T, NetEntityId>)
                {
                    uint32_t parsed = 0;
                    valid = ParseNumber(text, parsed);
                    if (valid)
                    {
                        item = NetEntityId(parsed);
                    }
                }
                else
                {
                    constexpr size_t count = AZStd::is_same_v<T, AZ::Vector2> ? 2 : AZStd::is_same_v<T, AZ::Vector3> ? 3 : 4;
                    float components[count]{};
                    auto remaining = text;
                    valid = true;
                    for (size_t i = 0; i < count; ++i)
                    {
                        const auto separator = remaining.find(',');
                        auto part = remaining.substr(0, separator);
                        while (!part.empty() && part.front() == ' ')
                        {
                            part.remove_prefix(1);
                        }
                        while (!part.empty() && part.back() == ' ')
                        {
                            part.remove_suffix(1);
                        }
                        if (part.empty() || !ParseNumber(part, components[i]) || !std::isfinite(components[i]) ||
                            ((separator == AZStd::string_view::npos) != (i == count - 1)))
                        {
                            valid = false;
                            break;
                        }
                        remaining.remove_prefix(separator == AZStd::string_view::npos ? remaining.size() : separator + 1);
                    }
                    if (valid)
                    {
                        if constexpr (AZStd::is_same_v<T, AZ::Vector2>)
                        {
                            item = AZ::Vector2(components[0], components[1]);
                        }
                        else if constexpr (AZStd::is_same_v<T, AZ::Vector3>)
                        {
                            item = AZ::Vector3(components[0], components[1], components[2]);
                        }
                        else
                        {
                            item = AZ::Quaternion(components[0], components[1], components[2], components[3]);
                        }
                    }
                }
            },
            value);
        if (!valid)
        {
            return AZ::Failure(AZStd::string::format("Invalid default '%.*s'", aznumeric_cast<int>(text.size()), text.data()));
        }
        return AZ::Success(value);
    }

    AZStd::string FormatScriptDefinedValue(const ScriptDefinedRpcValue& value)
    {
        return AZStd::visit(
            [](const auto& item) -> AZStd::string
            {
                using T = AZStd::decay_t<decltype(item)>;
                if constexpr (AZStd::is_same_v<T, bool>)
                {
                    return item ? "true" : "false";
                }
                else if constexpr (AZStd::is_integral_v<T> || AZStd::is_floating_point_v<T>)
                {
                    return FormatNumber(item);
                }
                else if constexpr (AZStd::is_same_v<T, ScriptDefinedString>)
                {
                    return AZStd::string(item.data(), item.size());
                }
                else if constexpr (AZStd::is_same_v<T, NetEntityId>)
                {
                    return FormatNumber(static_cast<uint32_t>(item));
                }
                else if constexpr (AZStd::is_same_v<T, AZ::Vector2>)
                {
                    return FormatNumber(item.GetX()) + "," + FormatNumber(item.GetY());
                }
                else if constexpr (AZStd::is_same_v<T, AZ::Vector3>)
                {
                    return FormatNumber(item.GetX()) + "," + FormatNumber(item.GetY()) + "," + FormatNumber(item.GetZ());
                }
                else
                {
                    return FormatNumber(item.GetX()) + "," + FormatNumber(item.GetY()) + "," + FormatNumber(item.GetZ()) + "," +
                        FormatNumber(item.GetW());
                }
            },
            value);
    }

    AZ::Outcome<void, AZStd::string> MultiplayerScriptDefinedDefinition::Validate() const
    {
        if (m_id.IsNull() || !m_revision || !IsScriptDefinedIdentifier(m_name))
        {
            return AZ::Failure(AZStd::string("Definition needs a stable ID, revision and an identifier name"));
        }
        if (m_inputs.size() > ScriptDefinedInputCapacity || m_rpcs.size() > ScriptDefinedRpcCapacity || m_archetypes.size() > 256)
        {
            return AZ::Failure(AZStd::string("Too many inputs (8), RPCs (16), or archetypes (256)"));
        }
        AZStd::unordered_set<AZ::Uuid> ids;
        AZStd::unordered_set<AZStd::string> names;
        auto check = [&](const ScriptDefinedParameter& field)
        {
            return !field.m_id.IsNull() && IsScriptDefinedIdentifier(field.m_name) &&
                static_cast<unsigned>(field.m_type) <= static_cast<unsigned>(ScriptDefinedRpcType::Entity) &&
                ids.insert(field.m_id).second && names.insert(field.m_name).second;
        };
        unsigned counts[ScriptDefinedPropertyBankCount]{};
        for (const auto* fields : { &m_properties, &m_inputs, &m_archetypes })
        {
            for (const auto& field : *fields)
            {
                if (!check(field) || !ParseScriptDefinedValue(field.m_type, field.m_defaultValue))
                {
                    return AZ::Failure(AZStd::string::format("Invalid or duplicate field: %s", field.m_name.c_str()));
                }
                if (fields == &m_properties && ++counts[static_cast<unsigned>(field.m_type)] > ScriptDefinedPropertyCapacity)
                {
                    return AZ::Failure(AZStd::string("A property type exceeds its 16-element vector capacity"));
                }
            }
        }
        for (const auto& rpc : m_rpcs)
        {
            if (rpc.m_id.IsNull() || !IsScriptDefinedIdentifier(rpc.m_name) || !ids.insert(rpc.m_id).second ||
                !names.insert(rpc.m_name).second || !ValidRoute(rpc.m_delivery) || rpc.m_parameters.size() > ScriptDefinedArgumentCapacity)
            {
                return AZ::Failure(AZStd::string::format("Invalid or duplicate RPC: %s", rpc.m_name.c_str()));
            }
            AZStd::unordered_set<AZStd::string> parameterNames;
            for (const auto& parameter : rpc.m_parameters)
            {
                if (parameter.m_id.IsNull() || !ids.insert(parameter.m_id).second || !IsScriptDefinedIdentifier(parameter.m_name) ||
                    !parameterNames.insert(parameter.m_name).second ||
                    static_cast<unsigned>(parameter.m_type) > static_cast<unsigned>(ScriptDefinedRpcType::Entity))
                {
                    return AZ::Failure(AZStd::string::format("Invalid parameter in %s", rpc.m_name.c_str()));
                }
            }
        }
        return AZ::Success();
    }

    AZ::u64 MultiplayerScriptDefinedDefinition::GetFingerprint() const
    {
        AZStd::string canonical = m_id.ToFixedString().c_str();
        auto append = [&](AZStd::string_view text)
        {
            canonical += AZStd::string::format("%zu:", text.size());
            canonical.append(text.data(), text.size());
        };
        for (const auto* fields : { &m_properties, &m_inputs, &m_archetypes })
        {
            append("|");
            for (const auto* field : Sorted(*fields))
            {
                append(field->m_id.ToFixedString().c_str());
                append(AZStd::string::format(
                    "%u:%u", static_cast<unsigned>(field->m_type), fields == &m_archetypes ? false : field->m_predictable));
                auto parsed = ParseScriptDefinedValue(field->m_type, field->m_defaultValue);
                if (fields != &m_archetypes)
                {
                    append(parsed ? FormatScriptDefinedValue(parsed.GetValue()) : field->m_defaultValue);
                }
            }
        }
        for (const auto* rpc : Sorted(m_rpcs))
        {
            append(rpc->m_id.ToFixedString().c_str());
            append(AZStd::string::format("%u:%u", static_cast<unsigned>(rpc->m_delivery), rpc->m_isReliable));
            for (const auto& parameter : rpc->m_parameters)
            {
                append(parameter.m_id.ToFixedString().c_str());
                append(AZStd::string::format("%u", static_cast<unsigned>(parameter.m_type)));
            }
            append("|");
        }
        const auto hash = static_cast<AZ::u64>(AZ::TypeHash64(reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size()));
        return hash ? hash : 1;
    }

    uint16_t MultiplayerScriptDefinedDefinition::GetRpcId(const AZ::Uuid& id) const
    {
        const auto sorted = Sorted(m_rpcs);
        for (size_t i = 0; i < sorted.size(); ++i)
        {
            if (sorted[i]->m_id == id)
            {
                return aznumeric_cast<uint16_t>(i + 1);
            }
        }
        return 0;
    }
    uint32_t MultiplayerScriptDefinedDefinition::GetPropertySlot(const AZ::Uuid& id) const
    {
        const auto* field = FindProperty(id);
        if (!field)
        {
            return ScriptDefinedPropertyCapacity;
        }
        uint32_t slot = 0;
        for (const auto& other : m_properties)
        {
            if (other.m_type == field->m_type && other.m_id < id)
            {
                ++slot;
            }
        }
        return slot;
    }
    AZStd::vector<ScriptDefinedRpcSignature> MultiplayerScriptDefinedDefinition::GetRpcSignatures() const
    {
        AZStd::vector<ScriptDefinedRpcSignature> result;
        for (const auto& rpc : m_rpcs)
        {
            ScriptDefinedRpcSignature signature;
            signature.m_rpcId = GetRpcId(rpc.m_id);
            signature.m_delivery = rpc.m_delivery;
            signature.m_isReliable = rpc.m_isReliable;
            for (const auto& parameter : rpc.m_parameters)
            {
                signature.m_parameterTypes.push_back(parameter.m_type);
            }
            result.push_back(signature);
        }
        return result;
    }
    const ScriptDefinedField* MultiplayerScriptDefinedDefinition::FindProperty(const AZ::Uuid& id) const
    {
        for (const auto& field : m_properties)
        {
            if (field.m_id == id)
            {
                return &field;
            }
        }
        return nullptr;
    }
    const ScriptDefinedField* MultiplayerScriptDefinedDefinition::FindArchetype(const AZ::Uuid& id) const
    {
        for (const auto& field : m_archetypes)
        {
            if (field.m_id == id)
            {
                return &field;
            }
        }
        return nullptr;
    }
    const ScriptDefinedRpcDefinition* MultiplayerScriptDefinedDefinition::FindRpc(const AZ::Uuid& id) const
    {
        for (const auto& rpc : m_rpcs)
        {
            if (rpc.m_id == id)
            {
                return &rpc;
            }
        }
        return nullptr;
    }

    void ScriptDefinedParameter::Reflect(AZ::ReflectContext* context)
    {
        auto* sc = azrtti_cast<AZ::SerializeContext*>(context);
        if (!sc || sc->FindClassData(azrtti_typeid<ScriptDefinedParameter>()))
        {
            return;
        }
        sc->Enum<ScriptDefinedRpcType>()
            ->Value("Boolean", ScriptDefinedRpcType::Boolean)
            ->Value("Int32", ScriptDefinedRpcType::Int32)
            ->Value("Float", ScriptDefinedRpcType::Float)
            ->Value("Vector3", ScriptDefinedRpcType::Vector3)
            ->Value("Int8", ScriptDefinedRpcType::Int8)
            ->Value("UInt8", ScriptDefinedRpcType::UInt8)
            ->Value("Int16", ScriptDefinedRpcType::Int16)
            ->Value("UInt16", ScriptDefinedRpcType::UInt16)
            ->Value("UInt32", ScriptDefinedRpcType::UInt32)
            ->Value("Int64", ScriptDefinedRpcType::Int64)
            ->Value("UInt64", ScriptDefinedRpcType::UInt64)
            ->Value("Double", ScriptDefinedRpcType::Double)
            ->Value("Vector2", ScriptDefinedRpcType::Vector2)
            ->Value("Quaternion", ScriptDefinedRpcType::Quaternion)
            ->Value("String", ScriptDefinedRpcType::String)
            ->Value("Entity", ScriptDefinedRpcType::Entity);
        sc->Class<ScriptDefinedParameter>()
            ->Version(1)
            ->Field("Id", &ScriptDefinedParameter::m_id)
            ->Field("Name", &ScriptDefinedParameter::m_name)
            ->Field("Type", &ScriptDefinedParameter::m_type);
        if (auto* ec = sc->GetEditContext())
        {
            ec->Class<ScriptDefinedParameter>("Parameter", "Named typed parameter")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::NameLabelOverride, &ScriptDefinedParameter::GetLabel)
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(AZ::Edit::UIHandlers::Default, &ScriptDefinedParameter::m_name, "Name", "Identifier used for graph nodes")
                ->DataElement(AZ::Edit::UIHandlers::ComboBox, &ScriptDefinedParameter::m_type, "Type", "Native wire type")
                ->EnumAttribute(ScriptDefinedRpcType::Boolean, "Boolean")
                ->EnumAttribute(ScriptDefinedRpcType::Int32, "Int32")
                ->EnumAttribute(ScriptDefinedRpcType::Float, "Float")
                ->EnumAttribute(ScriptDefinedRpcType::Vector3, "Vector3")
                ->EnumAttribute(ScriptDefinedRpcType::Int8, "Int8")
                ->EnumAttribute(ScriptDefinedRpcType::UInt8, "UInt8")
                ->EnumAttribute(ScriptDefinedRpcType::Int16, "Int16")
                ->EnumAttribute(ScriptDefinedRpcType::UInt16, "UInt16")
                ->EnumAttribute(ScriptDefinedRpcType::UInt32, "UInt32")
                ->EnumAttribute(ScriptDefinedRpcType::Int64, "Int64")
                ->EnumAttribute(ScriptDefinedRpcType::UInt64, "UInt64")
                ->EnumAttribute(ScriptDefinedRpcType::Double, "Double")
                ->EnumAttribute(ScriptDefinedRpcType::Vector2, "Vector2")
                ->EnumAttribute(ScriptDefinedRpcType::Quaternion, "Quaternion")
                ->EnumAttribute(ScriptDefinedRpcType::String, "String")
                ->EnumAttribute(ScriptDefinedRpcType::Entity, "Entity");
        }
    }
    void ScriptDefinedField::Reflect(AZ::ReflectContext* context)
    {
        ScriptDefinedParameter::Reflect(context);
        auto* sc = azrtti_cast<AZ::SerializeContext*>(context);
        if (!sc || sc->FindClassData(azrtti_typeid<ScriptDefinedField>()))
        {
            return;
        }
        sc->Class<ScriptDefinedField, ScriptDefinedParameter>()
            ->Version(1)
            ->Field("Default", &ScriptDefinedField::m_defaultValue)
            ->Field("Predictable", &ScriptDefinedField::m_predictable);
        if (auto* ec = sc->GetEditContext())
        {
            ec->Class<ScriptDefinedField>("Field", "Typed field")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::NameLabelOverride, &ScriptDefinedField::GetLabel)
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(
                    AZ::Edit::UIHandlers::Default,
                    &ScriptDefinedField::m_defaultValue,
                    "Default",
                    "Empty uses the type default. Vectors/quaternion: comma-separated components. Integers: decimal. Strings: up to 64 "
                    "bytes.")
                ->DataElement(
                    AZ::Edit::UIHandlers::Default,
                    &ScriptDefinedField::m_predictable,
                    "Predictable",
                    "Allow autonomous property writes during input prediction");
        }
    }
    void ScriptDefinedRpcDefinition::Reflect(AZ::ReflectContext* context)
    {
        ScriptDefinedParameter::Reflect(context);
        auto* sc = azrtti_cast<AZ::SerializeContext*>(context);
        if (!sc || sc->FindClassData(azrtti_typeid<ScriptDefinedRpcDefinition>()))
        {
            return;
        }
        sc->Class<ScriptDefinedRpcDefinition>()
            ->Version(1)
            ->Field("Id", &ScriptDefinedRpcDefinition::m_id)
            ->Field("Name", &ScriptDefinedRpcDefinition::m_name)
            ->Field("Delivery", &ScriptDefinedRpcDefinition::m_delivery)
            ->Field("Reliable", &ScriptDefinedRpcDefinition::m_isReliable)
            ->Field("Parameters", &ScriptDefinedRpcDefinition::m_parameters);
        if (auto* ec = sc->GetEditContext())
        {
            ec->Class<ScriptDefinedRpcDefinition>("RPC", "Named RPC with typed parameters")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::NameLabelOverride, &ScriptDefinedRpcDefinition::GetLabel)
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(AZ::Edit::UIHandlers::Default, &ScriptDefinedRpcDefinition::m_name, "Name", "")
                ->DataElement(AZ::Edit::UIHandlers::ComboBox, &ScriptDefinedRpcDefinition::m_delivery, "Delivery", "")
                ->EnumAttribute(RpcDeliveryType::AuthorityToClient, "Authority to clients")
                ->EnumAttribute(RpcDeliveryType::AutonomousToAuthority, "Autonomous to authority")
                ->EnumAttribute(RpcDeliveryType::AuthorityToAutonomous, "Authority to autonomous")
                ->EnumAttribute(RpcDeliveryType::ServerToAuthority, "Server to authority")
                ->DataElement(AZ::Edit::UIHandlers::Default, &ScriptDefinedRpcDefinition::m_isReliable, "Reliable", "")
                ->DataElement(
                    AZ::Edit::UIHandlers::Default, &ScriptDefinedRpcDefinition::m_parameters, "Parameters", "Up to eight parameters");
        }
    }
    void MultiplayerScriptDefinedDefinition::Reflect(AZ::ReflectContext* context)
    {
        ScriptDefinedField::Reflect(context);
        ScriptDefinedRpcDefinition::Reflect(context);
        auto* sc = azrtti_cast<AZ::SerializeContext*>(context);
        if (!sc || sc->FindClassData(azrtti_typeid<MultiplayerScriptDefinedDefinition>()))
        {
            return;
        }
        sc->Class<MultiplayerScriptDefinedDefinition>()
            ->Version(1)
            ->Field("Id", &MultiplayerScriptDefinedDefinition::m_id)
            ->Field("Name", &MultiplayerScriptDefinedDefinition::m_name)
            ->Field("Revision", &MultiplayerScriptDefinedDefinition::m_revision)
            ->Field("Properties", &MultiplayerScriptDefinedDefinition::m_properties)
            ->Field("Inputs", &MultiplayerScriptDefinedDefinition::m_inputs)
            ->Field("Archetypes", &MultiplayerScriptDefinedDefinition::m_archetypes)
            ->Field("Rpcs", &MultiplayerScriptDefinedDefinition::m_rpcs);
        if (auto* ec = sc->GetEditContext())
        {
            ec->Class<MultiplayerScriptDefinedDefinition>("Multiplayer definition", "Typed multiplayer schema")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(
                    0, &MultiplayerScriptDefinedDefinition::m_name, "Name", "Unique identifier for the generated Script Events interface")
                ->DataElement(
                    0, &MultiplayerScriptDefinedDefinition::m_properties, "Properties", "Replicated typed vectors; 16 elements per type")
                ->DataElement(0, &MultiplayerScriptDefinedDefinition::m_inputs, "Inputs", "Optional input layout; eight fields")
                ->DataElement(0, &MultiplayerScriptDefinedDefinition::m_archetypes, "Archetypes", "Read-only prefab configuration")
                ->DataElement(0, &MultiplayerScriptDefinedDefinition::m_rpcs, "RPCs", "Up to 16 named RPCs");
        }
    }
    void MultiplayerScriptDefinedAsset::Reflect(AZ::ReflectContext* context)
    {
        MultiplayerScriptDefinedDefinition::Reflect(context);
        auto* sc = azrtti_cast<AZ::SerializeContext*>(context);
        if (!sc || sc->FindClassData(azrtti_typeid<MultiplayerScriptDefinedAsset>()))
        {
            return;
        }
        sc->Class<MultiplayerScriptDefinedAsset, AZ::Data::AssetData>()
            ->Version(1)
            ->Attribute(AZ::Edit::Attributes::EnableForAssetEditor, true)
            ->Field("Definition", &MultiplayerScriptDefinedAsset::m_definition)
            ->Field("Validation", &MultiplayerScriptDefinedAsset::m_validation);
        if (auto* ec = sc->GetEditContext())
        {
            ec->Class<MultiplayerScriptDefinedAsset>("Multiplayer Script Definition", "Code-free multiplayer authoring")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::EnableForAssetEditor, true)
                ->DataElement(0, &MultiplayerScriptDefinedAsset::m_definition, "Definition", "")
                ->Attribute(AZ::Edit::Attributes::ChangeNotify, &MultiplayerScriptDefinedAsset::ValidateForEditor)
                ->DataElement(0, &MultiplayerScriptDefinedAsset::m_validation, "Validation", "")
                ->Attribute(AZ::Edit::Attributes::ReadOnly, true);
        }
    }
    AZ::u32 MultiplayerScriptDefinedAsset::ValidateForEditor()
    {
        const auto validation = m_definition.Validate();
        m_validation = validation ? "Valid" : validation.GetError();
        return AZ::Edit::PropertyRefreshLevels::EntireTree;
    }
} // namespace Multiplayer
