/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/StringFunc/StringFunc.h>
#include <AzCore/Utils/TypeHash.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/containers/array.h>
#include <AzCore/std/containers/unordered_set.h>
#include <AzCore/std/sort.h>
#include <AzCore/std/typetraits/void_t.h>
#include <AzCore/std/string/conversions.h>
#include <Multiplayer/MultiplayerScriptDefinedDefinition.h>
#include <AzCore/std/utility/charconv.h>
#include <cmath>
#include <limits>

namespace Multiplayer
{
    namespace
    {
        //! Large enough for any integer and for a double at max_digits10 precision.
        constexpr size_t FormattedNumberCapacity = 64;

        //! Text of the separator between vector and quaternion components.
        constexpr char ComponentSeparator = ',';

        //! Parses the whole text as a number. Trailing characters make the parse fail.
        template<class T>
        bool ParseNumber(AZStd::string_view text, T& value)
        {
            const char* end = text.data() + text.size();
            const auto result = AZStd::from_chars(text.data(), end, value);
            return result.ec == AZStd::errc{} && result.ptr == end;
        }

        //! Formats a number so that ParseNumber reads back exactly the same value.
        template<class T>
        AZStd::string FormatNumber(T value)
        {
            AZStd::array<char, FormattedNumberCapacity> buffer;
            char* end = buffer.data();
            if constexpr (AZStd::is_floating_point_v<T>)
            {
                constexpr int precision = std::numeric_limits<T>::max_digits10;
                end = AZStd::to_chars(buffer.data(), buffer.data() + buffer.size(), value, AZStd::chars_format::general, precision).ptr;
            }
            else
            {
                end = AZStd::to_chars(buffer.data(), buffer.data() + buffer.size(), value).ptr;
            }
            return AZStd::string(buffer.data(), end);
        }

        //! Returns pointers to the members sorted by ID, the order that wire IDs and fingerprints use.
        template<class Range>
        auto SortedById(const Range& members)
        {
            using Element = typename Range::value_type;

            AZStd::vector<const Element*> result;
            for (const auto& member : members)
            {
                result.push_back(&member);
            }

            AZStd::sort(
                result.begin(),
                result.end(),
                [](const Element* lhs, const Element* rhs)
                {
                    return lhs->m_id < rhs->m_id;
                });
            return result;
        }

        bool IsSupportedDeliveryRoute(RpcDeliveryType route)
        {
            return route == RpcDeliveryType::AuthorityToClient || route == RpcDeliveryType::AuthorityToAutonomous ||
                route == RpcDeliveryType::AutonomousToAuthority || route == RpcDeliveryType::ServerToAuthority;
        }

        bool IsKnownType(ScriptDefinedRpcType type)
        {
            return static_cast<uint32_t>(type) < ScriptDefinedTypeCount;
        }

        //! @{
        //! True for the math types that have a CreateIdentity() or a CreateZero() factory.
        template<class T, class = void>
        struct HasCreateIdentity : AZStd::false_type
        {
        };

        template<class T>
        struct HasCreateIdentity<T, AZStd::void_t<decltype(T::CreateIdentity())>> : AZStd::true_type
        {
        };

        template<class T, class = void>
        struct HasCreateZero : AZStd::false_type
        {
        };

        template<class T>
        struct HasCreateZero<T, AZStd::void_t<decltype(T::CreateZero())>> : AZStd::true_type
        {
        };
        //! @}

        //! Returns the default of a value type: identity for rotations, transforms and matrices, zero for vectors and
        //! numbers, false, an empty string, or an invalid entity.
        template<class T>
        T MakeDefaultValue()
        {
            if constexpr (HasCreateIdentity<T>::value)
            {
                return T::CreateIdentity();
            }
            else if constexpr (HasCreateZero<T>::value)
            {
                return T::CreateZero();
            }
            else if constexpr (AZStd::is_same_v<T, NetEntityId>)
            {
                return InvalidNetEntityId;
            }
            else
            {
                return T{};
            }
        }

        //! Converts a math type to and from the float components of its text form. Every math type in
        //! MULTIPLAYER_SCRIPT_DEFINED_TYPES needs a specialization.
        template<class T>
        struct MathComponents;

        template<>
        struct MathComponents<AZ::Vector2>
        {
            static constexpr size_t Count = 2;

            static void Store(const AZ::Vector2& value, float* components)
            {
                value.StoreToFloat2(components);
            }

            static AZ::Vector2 Create(const float* components)
            {
                return AZ::Vector2::CreateFromFloat2(components);
            }
        };

        template<>
        struct MathComponents<AZ::Vector3>
        {
            static constexpr size_t Count = 3;

            static void Store(const AZ::Vector3& value, float* components)
            {
                value.StoreToFloat3(components);
            }

            static AZ::Vector3 Create(const float* components)
            {
                return AZ::Vector3::CreateFromFloat3(components);
            }
        };

        template<>
        struct MathComponents<AZ::Vector4>
        {
            static constexpr size_t Count = 4;

            static void Store(const AZ::Vector4& value, float* components)
            {
                value.StoreToFloat4(components);
            }

            static AZ::Vector4 Create(const float* components)
            {
                return AZ::Vector4::CreateFromFloat4(components);
            }
        };

        template<>
        struct MathComponents<AZ::Quaternion>
        {
            static constexpr size_t Count = 4;

            static void Store(const AZ::Quaternion& value, float* components)
            {
                value.StoreToFloat4(components);
            }

            static AZ::Quaternion Create(const float* components)
            {
                return AZ::Quaternion::CreateFromFloat4(components);
            }
        };

        //! A transform is its translation, its rotation quaternion and its uniform scale.
        template<>
        struct MathComponents<AZ::Transform>
        {
            static constexpr size_t TranslationCount = 3;
            static constexpr size_t RotationCount = 4;
            static constexpr size_t Count = TranslationCount + RotationCount + 1;

            static void Store(const AZ::Transform& value, float* components)
            {
                value.GetTranslation().StoreToFloat3(components);
                value.GetRotation().StoreToFloat4(components + TranslationCount);
                components[TranslationCount + RotationCount] = value.GetUniformScale();
            }

            static AZ::Transform Create(const float* components)
            {
                AZ::Transform value = AZ::Transform::CreateFromQuaternionAndTranslation(
                    AZ::Quaternion::CreateFromFloat4(components + TranslationCount), AZ::Vector3::CreateFromFloat3(components));
                value.SetUniformScale(components[TranslationCount + RotationCount]);
                return value;
            }
        };

        template<>
        struct MathComponents<AZ::Matrix3x3>
        {
            static constexpr size_t Count = 9;

            static void Store(const AZ::Matrix3x3& value, float* components)
            {
                value.StoreToRowMajorFloat9(components);
            }

            static AZ::Matrix3x3 Create(const float* components)
            {
                return AZ::Matrix3x3::CreateFromRowMajorFloat9(components);
            }
        };

        template<>
        struct MathComponents<AZ::Matrix4x4>
        {
            static constexpr size_t Count = 16;

            static void Store(const AZ::Matrix4x4& value, float* components)
            {
                value.StoreToRowMajorFloat16(components);
            }

            static AZ::Matrix4x4 Create(const float* components)
            {
                return AZ::Matrix4x4::CreateFromRowMajorFloat16(components);
            }
        };

        //! Parses comma-separated components. Spaces around a component are ignored.
        //! Fails unless the text holds exactly ComponentCount finite numbers.
        template<size_t ComponentCount>
        bool ParseComponents(AZStd::string_view text, AZStd::array<float, ComponentCount>& components)
        {
            AZStd::string_view remaining = text;
            for (size_t index = 0; index < components.size(); ++index)
            {
                const size_t separator = remaining.find(ComponentSeparator);
                const AZStd::string_view component = AZ::StringFunc::StripEnds(remaining.substr(0, separator));

                // Every component but the last one is followed by a separator.
                const bool isLast = index == components.size() - 1;
                const bool hasSeparator = separator != AZStd::string_view::npos;
                if (component.empty() || !ParseNumber(component, components[index]) || !std::isfinite(components[index]) ||
                    hasSeparator == isLast)
                {
                    return false;
                }

                remaining.remove_prefix(hasSeparator ? separator + 1 : remaining.size());
            }
            return true;
        }

        //! Parses the text form of one value into item, which already holds the type's default.
        template<class T>
        bool ParseValueText(AZStd::string_view text, T& item)
        {
            if constexpr (AZStd::is_same_v<T, bool>)
            {
                item = text == "true" || text == "1";
                return item || text == "false" || text == "0";
            }
            else if constexpr (AZStd::is_integral_v<T> && AZStd::is_signed_v<T>)
            {
                // Parse at full width so that out-of-range text fails instead of wrapping.
                int64_t parsed = 0;
                if (!ParseNumber(text, parsed) || parsed < std::numeric_limits<T>::min() || parsed > std::numeric_limits<T>::max())
                {
                    return false;
                }
                item = static_cast<T>(parsed);
                return true;
            }
            else if constexpr (AZStd::is_integral_v<T>)
            {
                uint64_t parsed = 0;
                if (!ParseNumber(text, parsed) || parsed > std::numeric_limits<T>::max())
                {
                    return false;
                }
                item = static_cast<T>(parsed);
                return true;
            }
            else if constexpr (AZStd::is_floating_point_v<T>)
            {
                return ParseNumber(text, item) && std::isfinite(item);
            }
            else if constexpr (AZStd::is_same_v<T, ScriptDefinedString>)
            {
                if (text.size() > item.max_size())
                {
                    return false;
                }
                item.assign(text.data(), text.size());
                return true;
            }
            else if constexpr (AZStd::is_same_v<T, NetEntityId>)
            {
                AZ::u64 parsed = 0;
                if (!ParseNumber(text, parsed))
                {
                    return false;
                }
                item = NetEntityId(parsed);
                return true;
            }
            else
            {
                AZStd::array<float, MathComponents<T>::Count> components{};
                if (!ParseComponents(text, components))
                {
                    return false;
                }
                item = MathComponents<T>::Create(components.data());
                return true;
            }
        }

        //! Formats the components of a math type, separated by ComponentSeparator.
        template<class T>
        AZStd::string FormatComponents(const T& value)
        {
            AZStd::array<float, MathComponents<T>::Count> components{};
            MathComponents<T>::Store(value, components.data());

            AZStd::string result;
            for (size_t index = 0; index < components.size(); ++index)
            {
                if (index > 0)
                {
                    result += ComponentSeparator;
                }
                result += FormatNumber(components[index]);
            }
            return result;
        }
    } // namespace

    bool IsScriptDefinedIdentifier(AZStd::string_view name)
    {
        if (name.empty() || name.size() > ScriptDefinedIdentifierCapacity)
        {
            return false;
        }

        const auto isIdentifierStart = [](char character)
        {
            return AZStd::is_alpha(character) || character == '_';
        };
        const auto isIdentifierCharacter = [](char character)
        {
            return AZStd::is_alnum(character) || character == '_';
        };
        return isIdentifierStart(name.front()) && AZStd::all_of(name.begin(), name.end(), isIdentifierCharacter);
    }

    ScriptDefinedRpcValue DefaultScriptDefinedValue(ScriptDefinedRpcType type)
    {
        switch (type)
        {
#define MULTIPLAYER_SCRIPT_DEFINED_DEFAULT(EnumName, CppType)                                                                              \
    case ScriptDefinedRpcType::EnumName:                                                                                                   \
        return ScriptDefinedRpcValue(AZStd::in_place_type<CppType>, MakeDefaultValue<CppType>());
            MULTIPLAYER_SCRIPT_DEFINED_TYPES(MULTIPLAYER_SCRIPT_DEFINED_DEFAULT)
#undef MULTIPLAYER_SCRIPT_DEFINED_DEFAULT
        default:
            return false;
        }
    }

    AZ::Outcome<ScriptDefinedRpcValue, AZStd::string> ParseScriptDefinedValue(ScriptDefinedRpcType type, AZStd::string_view text)
    {
        if (!IsKnownType(type))
        {
            return AZ::Failure(AZStd::string("Unknown value type"));
        }

        ScriptDefinedRpcValue value = DefaultScriptDefinedValue(type);
        if (text.empty())
        {
            return AZ::Success(value);
        }

        const bool parsed = AZStd::visit(
            [text](auto& item)
            {
                return ParseValueText(text, item);
            },
            value);
        if (!parsed)
        {
            return AZ::Failure(AZStd::string::format("Invalid default '%.*s'", AZ_STRING_ARG(text)));
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
                    return FormatNumber(static_cast<AZ::u64>(item));
                }
                else
                {
                    return FormatComponents(item);
                }
            },
            value);
    }

    AZ::Outcome<void, AZStd::string> MultiplayerScriptDefinedDefinition::Validate() const
    {
        if (m_id.IsNull() || m_revision == 0 || !IsScriptDefinedIdentifier(m_name))
        {
            return AZ::Failure(AZStd::string("Definition needs a stable ID, revision and an identifier name"));
        }

        if (m_inputs.size() > ScriptDefinedInputCapacity || m_rpcs.size() > ScriptDefinedRpcCapacity ||
            m_archetypes.size() > ScriptDefinedArchetypeCapacity)
        {
            return AZ::Failure(AZStd::string::format(
                "Too many inputs (%u), RPCs (%u), or archetypes (%zu)",
                ScriptDefinedInputCapacity,
                ScriptDefinedRpcCapacity,
                ScriptDefinedArchetypeCapacity));
        }

        // Member IDs and names are unique across properties, inputs, archetypes and RPCs.
        AZStd::unordered_set<AZ::Uuid> ids;
        AZStd::unordered_set<AZStd::string> names;
        const auto isNewValidMember = [&ids, &names](const ScriptDefinedParameter& member)
        {
            return !member.m_id.IsNull() && IsScriptDefinedIdentifier(member.m_name) && IsKnownType(member.m_type) &&
                ids.insert(member.m_id).second && names.insert(member.m_name).second;
        };

        AZStd::array<unsigned, ScriptDefinedPropertyBankCount> propertiesPerType{};
        uint32_t rewindableCount = 0;
        for (const auto* fields : { &m_properties, &m_inputs, &m_archetypes })
        {
            for (const auto& field : *fields)
            {
                if (!isNewValidMember(field) || !ParseScriptDefinedValue(field.m_type, field.m_defaultValue))
                {
                    return AZ::Failure(AZStd::string::format("Invalid or duplicate field: %s", field.m_name.c_str()));
                }

                if (fields != &m_properties)
                {
                    continue;
                }

                // Rewindable properties share one bank; the others use the bank of their type.
                if (field.m_rewindable && ++rewindableCount > ScriptDefinedRewindableCapacity)
                {
                    return AZ::Failure(AZStd::string::format("More than %u rewindable properties", ScriptDefinedRewindableCapacity));
                }

                if (!field.m_rewindable && ++propertiesPerType[static_cast<uint32_t>(field.m_type)] > ScriptDefinedPropertyCapacity)
                {
                    return AZ::Failure(
                        AZStd::string::format("A property type exceeds its %u-element bank capacity", ScriptDefinedPropertyCapacity));
                }
            }
        }

        for (const auto& rpc : m_rpcs)
        {
            const bool validRpc = !rpc.m_id.IsNull() && IsScriptDefinedIdentifier(rpc.m_name) && ids.insert(rpc.m_id).second &&
                names.insert(rpc.m_name).second && IsSupportedDeliveryRoute(rpc.m_delivery) &&
                rpc.m_parameters.size() <= ScriptDefinedArgumentCapacity;
            if (!validRpc)
            {
                return AZ::Failure(AZStd::string::format("Invalid or duplicate RPC: %s", rpc.m_name.c_str()));
            }

            // Parameter names only need to be unique within their RPC.
            AZStd::unordered_set<AZStd::string> parameterNames;
            for (const auto& parameter : rpc.m_parameters)
            {
                const bool validParameter = !parameter.m_id.IsNull() && ids.insert(parameter.m_id).second &&
                    IsScriptDefinedIdentifier(parameter.m_name) && parameterNames.insert(parameter.m_name).second &&
                    IsKnownType(parameter.m_type);
                if (!validParameter)
                {
                    return AZ::Failure(AZStd::string::format("Invalid parameter in %s", rpc.m_name.c_str()));
                }
            }
        }
        return AZ::Success();
    }

    AZ::u64 MultiplayerScriptDefinedDefinition::GetFingerprint() const
    {
        // Build a canonical text of everything that affects the wire format, in ID order, and hash it.
        // Each entry is length-prefixed so that different entries cannot produce the same text.
        AZStd::string canonical = m_id.ToFixedString().c_str();
        const auto append = [&canonical](AZStd::string_view text)
        {
            canonical += AZStd::string::format("%zu:", text.size());
            canonical.append(text.data(), text.size());
        };

        for (const auto* fields : { &m_properties, &m_inputs, &m_archetypes })
        {
            // Archetypes are local configuration: their values, prediction and rewind flags do not travel on the wire.
            const bool isArchetype = fields == &m_archetypes;

            append("|");
            for (const ScriptDefinedField* field : SortedById(*fields))
            {
                const bool predictable = isArchetype ? false : field->m_predictable;
                const bool rewindable = isArchetype ? false : field->m_rewindable;
                append(field->m_id.ToFixedString().c_str());
                append(AZStd::string::format("%u:%u:%u", static_cast<unsigned>(field->m_type), predictable, rewindable));
                if (!isArchetype)
                {
                    // Hash the canonical form of the default so that equivalent spellings match.
                    const auto parsed = ParseScriptDefinedValue(field->m_type, field->m_defaultValue);
                    append(parsed ? FormatScriptDefinedValue(parsed.GetValue()) : field->m_defaultValue);
                }
            }
        }

        for (const ScriptDefinedRpcDefinition* rpc : SortedById(m_rpcs))
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

        // A fingerprint of 0 means "no schema", so a definition never produces it.
        return hash != 0 ? hash : 1;
    }

    uint16_t MultiplayerScriptDefinedDefinition::GetRpcId(const AZ::Uuid& id) const
    {
        // Wire IDs start at 1 in ID order; 0 means "no RPC".
        const auto sorted = SortedById(m_rpcs);
        for (size_t index = 0; index < sorted.size(); ++index)
        {
            if (sorted[index]->m_id == id)
            {
                return aznumeric_cast<uint16_t>(index + 1);
            }
        }
        return 0;
    }

    uint32_t MultiplayerScriptDefinedDefinition::GetPropertySlot(const AZ::Uuid& id) const
    {
        const ScriptDefinedField* field = FindProperty(id);
        if (!field)
        {
            return ScriptDefinedPropertyCapacity;
        }

        // A property's slot is the number of properties in the same bank with a smaller ID. Rewindable properties of every
        // type share the rewindable bank; each other property is in the bank of its type.
        uint32_t slot = 0;
        for (const auto& other : m_properties)
        {
            const bool sameBank = other.m_rewindable == field->m_rewindable && (field->m_rewindable || other.m_type == field->m_type);
            if (sameBank && other.m_id < id)
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
        auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (!serializeContext || serializeContext->FindClassData(azrtti_typeid<ScriptDefinedParameter>()))
        {
            return;
        }

        // Schemas store type names, so the enum values are free to change.
        auto typeEnum = serializeContext->Enum<ScriptDefinedRpcType>();
#define MULTIPLAYER_SCRIPT_DEFINED_ENUM_VALUE(EnumName, CppType) typeEnum->Value(#EnumName, ScriptDefinedRpcType::EnumName);
        MULTIPLAYER_SCRIPT_DEFINED_TYPES(MULTIPLAYER_SCRIPT_DEFINED_ENUM_VALUE)
#undef MULTIPLAYER_SCRIPT_DEFINED_ENUM_VALUE

        serializeContext->Class<ScriptDefinedParameter>()
            ->Version(1)
            ->Field("Id", &ScriptDefinedParameter::m_id)
            ->Field("Name", &ScriptDefinedParameter::m_name)
            ->Field("Type", &ScriptDefinedParameter::m_type);

        if (AZ::EditContext* editContext = serializeContext->GetEditContext())
        {
            auto parameterEdit = editContext->Class<ScriptDefinedParameter>("Parameter", "Named typed parameter");
            parameterEdit->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::NameLabelOverride, &ScriptDefinedParameter::GetLabel)
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(AZ::Edit::UIHandlers::Default, &ScriptDefinedParameter::m_name, "Name", "Identifier used for graph nodes")
                ->DataElement(AZ::Edit::UIHandlers::ComboBox, &ScriptDefinedParameter::m_type, "Type", "Native wire type");
#define MULTIPLAYER_SCRIPT_DEFINED_ENUM_ATTRIBUTE(EnumName, CppType)                                                                       \
    parameterEdit->EnumAttribute(ScriptDefinedRpcType::EnumName, #EnumName);
            MULTIPLAYER_SCRIPT_DEFINED_TYPES(MULTIPLAYER_SCRIPT_DEFINED_ENUM_ATTRIBUTE)
#undef MULTIPLAYER_SCRIPT_DEFINED_ENUM_ATTRIBUTE
        }
    }

    void ScriptDefinedField::Reflect(AZ::ReflectContext* context)
    {
        ScriptDefinedParameter::Reflect(context);

        auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (!serializeContext || serializeContext->FindClassData(azrtti_typeid<ScriptDefinedField>()))
        {
            return;
        }

        serializeContext->Class<ScriptDefinedField, ScriptDefinedParameter>()
            ->Version(1)
            ->Field("Default", &ScriptDefinedField::m_defaultValue)
            ->Field("Predictable", &ScriptDefinedField::m_predictable)
            ->Field("Rewindable", &ScriptDefinedField::m_rewindable);

        if (AZ::EditContext* editContext = serializeContext->GetEditContext())
        {
            editContext->Class<ScriptDefinedField>("Field", "Typed field")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::NameLabelOverride, &ScriptDefinedField::GetLabel)
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(
                    AZ::Edit::UIHandlers::Default,
                    &ScriptDefinedField::m_defaultValue,
                    "Default",
                    "Empty uses the type default. Integers: decimal. Strings: up to 64 bytes. Vectors and quaternions: comma-separated "
                    "components. Matrices: row-major elements. Transforms: translation x,y,z, rotation x,y,z,w, uniform scale.")
                ->DataElement(
                    AZ::Edit::UIHandlers::Default,
                    &ScriptDefinedField::m_predictable,
                    "Predictable",
                    "Properties only. Allow autonomous writes during input prediction and correct mispredictions; otherwise the "
                    "property is authority-only")
                ->DataElement(
                    AZ::Edit::UIHandlers::Default,
                    &ScriptDefinedField::m_rewindable,
                    "Rewindable",
                    "Properties only. Keep a rewind history so that server-side lag compensation reads the value of the rewound frame; "
                    "at most eight per schema");
        }
    }

    void ScriptDefinedRpcDefinition::Reflect(AZ::ReflectContext* context)
    {
        ScriptDefinedParameter::Reflect(context);

        auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (!serializeContext || serializeContext->FindClassData(azrtti_typeid<ScriptDefinedRpcDefinition>()))
        {
            return;
        }

        serializeContext->Class<ScriptDefinedRpcDefinition>()
            ->Version(1)
            ->Field("Id", &ScriptDefinedRpcDefinition::m_id)
            ->Field("Name", &ScriptDefinedRpcDefinition::m_name)
            ->Field("Delivery", &ScriptDefinedRpcDefinition::m_delivery)
            ->Field("Reliable", &ScriptDefinedRpcDefinition::m_isReliable)
            ->Field("Parameters", &ScriptDefinedRpcDefinition::m_parameters);

        if (AZ::EditContext* editContext = serializeContext->GetEditContext())
        {
            editContext->Class<ScriptDefinedRpcDefinition>("RPC", "Named RPC with typed parameters")
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

        auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (!serializeContext || serializeContext->FindClassData(azrtti_typeid<MultiplayerScriptDefinedDefinition>()))
        {
            return;
        }

        serializeContext->Class<MultiplayerScriptDefinedDefinition>()
            ->Version(1)
            ->Field("Id", &MultiplayerScriptDefinedDefinition::m_id)
            ->Field("Name", &MultiplayerScriptDefinedDefinition::m_name)
            ->Field("Revision", &MultiplayerScriptDefinedDefinition::m_revision)
            ->Field("Properties", &MultiplayerScriptDefinedDefinition::m_properties)
            ->Field("Inputs", &MultiplayerScriptDefinedDefinition::m_inputs)
            ->Field("Archetypes", &MultiplayerScriptDefinedDefinition::m_archetypes)
            ->Field("Rpcs", &MultiplayerScriptDefinedDefinition::m_rpcs);

        if (AZ::EditContext* editContext = serializeContext->GetEditContext())
        {
            editContext->Class<MultiplayerScriptDefinedDefinition>("Multiplayer definition", "Typed multiplayer schema")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                ->DataElement(
                    AZ::Edit::UIHandlers::Default,
                    &MultiplayerScriptDefinedDefinition::m_name,
                    "Name",
                    "Unique identifier for the generated Script Events interface")
                ->DataElement(
                    AZ::Edit::UIHandlers::Default,
                    &MultiplayerScriptDefinedDefinition::m_properties,
                    "Properties",
                    "Replicated typed properties; 16 per type, plus up to four rewindable properties of any type")
                ->DataElement(
                    AZ::Edit::UIHandlers::Default,
                    &MultiplayerScriptDefinedDefinition::m_inputs,
                    "Inputs",
                    "Optional input layout; eight fields")
                ->DataElement(
                    AZ::Edit::UIHandlers::Default,
                    &MultiplayerScriptDefinedDefinition::m_archetypes,
                    "Archetypes",
                    "Read-only prefab configuration")
                ->DataElement(AZ::Edit::UIHandlers::Default, &MultiplayerScriptDefinedDefinition::m_rpcs, "RPCs", "Up to 16 named RPCs");
        }
    }

    void MultiplayerScriptDefinedAsset::Reflect(AZ::ReflectContext* context)
    {
        MultiplayerScriptDefinedDefinition::Reflect(context);

        auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (!serializeContext || serializeContext->FindClassData(azrtti_typeid<MultiplayerScriptDefinedAsset>()))
        {
            return;
        }

        serializeContext->Class<MultiplayerScriptDefinedAsset, AZ::Data::AssetData>()
            ->Version(1)
            ->Attribute(AZ::Edit::Attributes::EnableForAssetEditor, true)
            ->Field("Definition", &MultiplayerScriptDefinedAsset::m_definition)
            ->Field("Validation", &MultiplayerScriptDefinedAsset::m_validation);

        if (AZ::EditContext* editContext = serializeContext->GetEditContext())
        {
            editContext->Class<MultiplayerScriptDefinedAsset>("Multiplayer Script Definition", "Code-free multiplayer authoring")
                ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::EnableForAssetEditor, true)
                ->DataElement(AZ::Edit::UIHandlers::Default, &MultiplayerScriptDefinedAsset::m_definition, "Definition", "")
                ->Attribute(AZ::Edit::Attributes::ChangeNotify, &MultiplayerScriptDefinedAsset::ValidateForEditor)
                ->DataElement(AZ::Edit::UIHandlers::Default, &MultiplayerScriptDefinedAsset::m_validation, "Validation", "")
                ->Attribute(AZ::Edit::Attributes::ReadOnly, true);
        }
    }

    const MultiplayerScriptDefinedDefinition& MultiplayerScriptDefinedAsset::GetDefinition() const
    {
        return m_definition;
    }

    MultiplayerScriptDefinedDefinition& MultiplayerScriptDefinedAsset::ModifyDefinition()
    {
        return m_definition;
    }

    void MultiplayerScriptDefinedAsset::SetDefinition(const MultiplayerScriptDefinedDefinition& definition)
    {
        m_definition = definition;
    }

    AZ::u32 MultiplayerScriptDefinedAsset::ValidateForEditor()
    {
        const auto validation = m_definition.Validate();
        m_validation = validation ? "Valid" : validation.GetError();
        return AZ::Edit::PropertyRefreshLevels::EntireTree;
    }
} // namespace Multiplayer
