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

Supported native types are Boolean, signed/unsigned 8/16/32/64-bit integers, Float, Double, Vector2, Vector3, Quaternion, String, and Entity.

Defaults are editable text so every integer width retains its full range:

- Empty: the type default (zero/false/empty; identity quaternion; invalid entity).
- Integers: decimal, with range checking.
- Boolean: `true`, `false`, `1`, or `0`.
- Vector2/Vector3/Quaternion: comma-separated finite components, for example `1,2,3` or `0,0,0,1`.
- String: up to 64 bytes.
- Entity: a native numeric NetEntityId. Script Canvas pins use EntityId and resolve through network bindings.

Script Canvas uses Number pins for Float, Double, and integers up to 32 bits. Integer setters reject fractional and out-of-range numbers. The exact **MultiplayerInt64** and **MultiplayerUInt64** value types provide **FromDecimal**, **ToDecimal**, **IsValid**, checked **Add**, **Equal**, and **Less** nodes. Use these for 64-bit fields/RPC parameters to retain all bits.

## Native bounds and network layout

- Each property type uses an AutoComponent `Container="Vector"` with capacity 16.
- A component reserves 272 property dirty bits: 16 banks × (16 elements + size). A stable element write dirties one bit; appending/removing also dirties the bank's size bit.
- The existing generator uses `AZStd::fixed_vector`: empty banks have no value payload, but their inline capacity and reserved record bits still occupy memory. See the native measurement tests for actual sizes.
- Changed property messages include an eight-byte schema fingerprint before bank values. Unchanged state sends no fingerprint/value payload. Mismatched schemas are rejected before interpreting values.
- Up to eight typed input fields. No declared inputs means no input companion, frame, callbacks, or additional input bytes.
- Up to 16 logical RPCs, each with up to eight typed parameters.
- RPC routes: authority → clients, autonomous → authority, authority → autonomous, and server → authority; each supports reliable or unreliable delivery.
- Archetypes are baked into the prefab and do not replicate. Their overridden values do not change the wire schema fingerprint.
- These properties use native prediction/correction records. They do not allocate rewind histories.

The builder produces schema sub-ID 0 and Script Events sub-ID 1 from the same source. It uses the Script Events job key so Script Canvas compilation waits for the generated interface.

## LocalMcp authoring

With LocalMcp's editor provider enabled, `binding_call` can invoke
`global/method/ScriptDefined_SaveSchema(path, definitionJson, overwrite)` and
`global/method/ScriptDefined_ReadSchema(path)`. These editor-only Automation
bindings use the native schema serializer and validator. Sources must be
`.mpschema` files inside the current project. Saves are atomic; replacing an
existing source requires `overwrite=true` and increases its revision.

Read a schema before editing it and preserve its field/parameter `Id` values.
The JSON members are the native serialized members (`Name`, `Properties`,
`Inputs`, `Archetypes`, `Rpcs`, etc.). LocalMcp's `asset_import`, component/property
tools, and `sc_*` native Script Canvas owner tools handle import, configuration,
graph wiring, save/reload, and attachment. No multiplayer-specific graph factory
or serialized graph patching is used.

The reproducible Windows acceptance runner and its exact pass conditions are in
[Code/Tests/Headless/README.md](Code/Tests/Headless/README.md).

## Verification

The native Multiplayer tests cover schema validation, EditContext reflection order, typed property replication, schema mismatch rejection, all RPC route/reliability combinations, and input gather/process/replay.

`Multiplayer_ScriptCanvas.Tests` covers editor export, generated method/parameter identity, checked numeric conversion, builder products, and calls through the real Script Events binding into runtime requests.

The 2026-11-05 Windows profile run passed all 200 Multiplayer tests (35 script-defined tests) and all eight integration tests. The headless MCP acceptance also passed: a separately launched native server received the scripted RPC and processed input, and the native client received Health=93, Ready=true, and LastMove=6.5. The run retained its MCP transcript, reloaded graph, attachment readback, cook diagnostics, and executable/module hashes under `build-script-defined/validation/headless-proof-12`.

The acceptance found and fixed two prefab lifecycle bugs: export must wait for its definition asset, and a loaded runtime component must reconstruct its hidden vector banks from the persisted definition before network attachment. Authored prediction flags now select the exact populated element/size bits in prediction records. The typed-property tests exercise serialized clones before attachment, including mixed predicted and authority-only fields.
