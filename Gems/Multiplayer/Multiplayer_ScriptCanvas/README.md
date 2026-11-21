# Multiplayer Script Defined components

Schema assets define native multiplayer state, optional input and typed RPCs; Script Canvas implements their behavior.

The editor declares the schema; Script Canvas implements its behavior. One definition asset supplies the typed graph interface and the native networking layout. No game-specific C++ or AutoComponent XML is needed.

## Author a component

1. Enable **Multiplayer**, **Script Canvas**, **Script Events**, and **Multiplayer Script Canvas**.
2. Open **Asset Editor**, create a **Multiplayer Script Definition**, and save it as a `.mpschema` asset in your project's Assets directory.
3. Give the definition a name. Add named, typed entries under **Properties**, **Inputs**, **Archetypes**, and **RPCs**. Names use letters, numbers, and underscores and start with a letter or underscore.
4. For an RPC, choose its delivery direction, reliability, and named typed parameters. Saving validates the schema. Asset Processor creates the compiled schema and its generated Script Events interface.
5. Add **Network Binding** and **Multiplayer Script Defined** to a network prefab entity. Select the definition asset in the component. The validation field reports schema errors.
6. Set prefab-specific **Archetypes** values by enabling their override checkboxes. These values are read-only during gameplay.
7. If the definition has inputs, also add **Local Prediction Player Input** (or your existing component providing `MultiplayerInputDriver`). Leave the driver out when no component on the entity needs input.
8. Attach a Script Canvas graph to the entity. Its node palette contains the definition's methods under **Multiplayer/<DefinitionName>**. The generated bus name has a UUID suffix so schemas with matching display names remain distinct.

Each schema save increases its Script Events revision. Field and parameter UUIDs preserve graph slot identity through renames and reordering. Deleting and recreating a field creates a new identity. Review/recompile graphs after signature changes. Stop play mode before editing a schema; running entities use the definition baked into their prefab.

## Script Canvas interface

Address every Script Event to the target network entity's **EntityId**, normally the graph owner's entity ID.

| Declaration | Send/request nodes | Receive/event nodes |
| --- | --- | --- |
| Property `Health` | `Get_Health`, `Set_Health(Value) -> Boolean` | `On_Health(Value)` |
| Input `Jump` | `GetInput_Jump`, `SetInput_Jump(Value) -> Boolean` | `OnCreateInput(DeltaTime)`, `OnProcessInput(DeltaTime, IsReprocessing)` |
| Archetype `Team` | `GetArchetype_Team` | — |
| RPC `Hit(Damage, Origin)` | `Send_Hit(Damage, Origin) -> Boolean` | `OnRpc_Hit(Damage, Origin)` |

Only declared fields/RPCs generate methods. The native transport endpoints are hidden from script reflection. Script Events itself offers sender/receiver nodes for every event; use the request and receive sides shown above.

- Gather input in **OnCreateInput** using the typed input setters.
- Read input and update gameplay in **OnProcessInput**. The same event runs during replay, with **IsReprocessing** set.
- Input setters return false outside gathering; input getters are available only inside gathering/processing callbacks.
- Property setters succeed on authority. **Predictable** properties also permit autonomous writes; perform those writes during input processing so replay can reproduce them.
- Property-change events run for accepted local changes and applied replicated element changes.
- RPC sends check the declared signature and the entity's network role. The receiver validates schema, argument types, logical RPC ID, direction, and reliability before notifying the graph.
- Check setter/send Boolean results. Getter nodes return the type default if the target component or input context is unavailable.

For example, declare a predictable Float property `MoveAmount`, a Float input `Move`, and a reliable autonomous-to-authority RPC `Use(Target: Entity)`. Gather `Move` in OnCreateInput, apply it to MoveAmount in OnProcessInput, and call Send_Use on the owning autonomous entity. Handle OnRpc_Use on authority.

## Types and defaults

Supported native types are Boolean, signed and unsigned 8/16/32/64-bit integers, Float, Double, Vector2, Vector3, Vector4, Quaternion, Transform, Matrix3x3, Matrix4x4, String, and Entity. Color and Matrix3x4 are not offered: Script Events cannot use them as parameter types, AzNetworking has no Color serializer, and Script Canvas has no Matrix3x4 data type.

Defaults are editable text so every integer width retains its full range:

- Empty: the type default (zero/false/empty; identity quaternion, transform and matrices; invalid entity).
- Integers: decimal, with range checking.
- Boolean: `true`, `false`, `1`, or `0`.
- Vector2/Vector3/Vector4/Quaternion: comma-separated finite components, for example `1,2,3` or `0,0,0,1`.
- Transform: translation x,y,z, rotation quaternion x,y,z,w, then uniform scale, for example `0,0,0,0,0,0,1,1`.
- Matrix3x3/Matrix4x4: 9 or 16 comma-separated elements in row-major order.
- String: up to 64 bytes.
- Entity: a native numeric NetEntityId. Script Canvas pins use EntityId and resolve through network bindings.

Script Canvas uses Number pins for Float, Double, and integers up to 32 bits. Integer setters reject fractional and out-of-range numbers. The exact **MultiplayerInt64** and **MultiplayerUInt64** value types provide **FromDecimal**, **ToDecimal**, **IsValid**, checked **Add**, **Equal**, and **Less** nodes. Use these for 64-bit fields/RPC parameters to retain all bits.

## Prediction and rewind

Each property has two independent flags:

- **Predictable**: the autonomous peer may write the property during input processing, and the property takes part in prediction records and corrections. Without it, the property is authority-only.
- **Rewindable**: the property keeps a history of its recent values, like a native `IsRewindable` network property. While the server rewinds time for lag compensation (for example, to check a hit against where a target was when the shooter fired), reads return the value at the rewound frame. Reads from the connection that owns the entity, and reads when time is not rewound, return the current value. A rewindable property cannot be written while time is rewound for another connection, because its history cannot be changed.

Rewindable properties are readable from network attachment on, when network time is available to record them.

## Native bounds and network layout

- Each type has an AutoComponent `Container="Vector"` bank with capacity 16 for its non-rewindable properties.
- Up to four rewindable properties, of any types, share one `Container="Array"` bank with `IsRewindable="true"`. Each element holds a tagged value and keeps 128 frames of history, whatever the schema declares; this memory is why the bank is small.
- A component reserves 344 property record bits: 20 typed banks × (16 elements + size), plus one bit per rewindable element. A stable element write dirties one bit; appending/removing also dirties the bank's size bit. A replication record allows 2,048 bits per entity.
- The banks reserve their capacity inline: empty banks have no value payload, but their inline capacity and reserved record bits still occupy memory. The native `Measurements` test prints the sizes.
- Changed property messages include an eight-byte schema fingerprint before bank values. Unchanged state sends no fingerprint/value payload. Mismatched schemas are rejected before interpreting values.
- Up to eight typed input fields. No declared inputs means no input companion, frame, callbacks, or additional input bytes.
- Up to 16 logical RPCs, each with up to eight typed parameters.
- RPC routes: authority → clients, autonomous → authority, authority → autonomous, and server → authority; each supports reliable or unreliable delivery.
- Archetypes are baked into the prefab and do not replicate. Their overridden values do not change the wire schema fingerprint.

The builder produces schema sub-ID 0 and Script Events sub-ID 1 from the same source. It uses the Script Events job key so Script Canvas compilation waits for the generated interface.

## Automation authoring

The editor-only Automation bindings `ScriptDefined_SaveSchema(path, definitionJson, overwrite)` and `ScriptDefined_ReadSchema(path)` use the native schema serializer and validator. Sources must be `.mpschema` files inside the current project. Saves are atomic; replacing an existing source requires `overwrite=true` and increases its revision.

Read a schema before editing it and preserve its field/parameter `Id` values. The JSON members are the native serialized members (`Name`, `Properties`, `Inputs`, `Archetypes`, `Rpcs`, etc.).

## Verification

The native Multiplayer tests cover schema validation, EditContext reflection order, typed property replication, rewindable history while time is rewound, predictable and authority-only writes, schema mismatch rejection, all RPC route/reliability combinations, and input gather/process/replay.

`Multiplayer_ScriptCanvas.Tests` covers editor export, generated method/parameter identity, pins for every value type, checked numeric conversion, builder products, and calls through the real Script Events binding into runtime requests.

A loaded runtime component reconstructs its hidden banks from the persisted definition before network attachment, and authored prediction flags select the exact populated element/size bits in prediction records. The typed-property tests exercise serialized clones before attachment, including mixed predicted, authority-only and rewindable fields.
