# Script-defined multiplayer validation

Authoring and Script Canvas usage are described in the [integration guide](../../../Multiplayer_ScriptCanvas/README.md).

Runtime code belongs to Multiplayer: shared definitions/wire types in `Multiplayer.Common.Static`, and generated components in the client/server/unified targets. Only fixtures and measurements belong to the test target.

## Review order

1. `MultiplayerScriptDefinedTypes.h`: `MULTIPLAYER_SCRIPT_DEFINED_TYPES` lists every value type once. The type enum, the value variant, reflected names, defaults, text parsing, bank access and Script Canvas pin types derive from it.
2. `Source/AutoGen/MultiplayerScriptDefined*.AutoComponent.xml`: one `{Type}Values` bank per listed type using `Container="Vector"`, the rewindable `RewindableValues` array, eight compiled RPC route/reliability endpoints, and the optional input companion. The component checks each bank's type, the capacity and the total record bits at compile time.
3. `MultiplayerScriptDefinedTypes.cpp`: bounded tagged values and transactional input/RPC decoding. Unknown tags and non-finite values are rejected.
4. `MultiplayerScriptDefinedDefinition`: typed defaults, bounds, stable IDs/slot mappings, and locale-independent schema fingerprints.
5. `MultiplayerScriptDefinedComponent`: defaults before attachment, populated-slot dirty marking before controllers exist, checked writes/RPC dispatch, rewind handling, and schema checks before property deserialization.
6. `MultiplayerScriptDefinedInputComponent`: immutable input layout, gather-only setters, input processing and replay.
7. `MultiplayerScriptDefinedComponentTests.cpp`: native integration, malformed input/RPC records, all RPC routes, rewindable history, reflection with EditContext, schema mismatch rejection, and measurements.

Both native components register their concrete SerializeContext class before calling generated base reflection. The base reflects the concrete class into EditContext; reversing this order crashes tool module loading.

## Core support

- `MultiplayerController::IsInputEnabled()` defaults to true. NetBind evaluates it while rebuilding input ordering, so omitted script-defined inputs add no frame/callbacks.
- Ownership updates visit every serialization component, including opted-out components and proxies.
- Generated RPC invocations stop when `SetRpcParams` fails.
- Initialization helpers are opt-in per XML component and reject changes after attachment. Components that opt in also get the first authority-to-client record bit of each property and their total bit count as constants.

## Running the tests

Build `Multiplayer.Tests` and `Multiplayer_ScriptCanvas.Tests`, then run AzTestRunner with each test module and `AzRunUnitTests`.

The `Measurements` test prints object sizes, record bits and bytes, initial/growth traffic, and single-element deltas for the build it runs in. Object sizes depend on the platform and compiler; the wire figures below do not.

## Wire costs

Each typed bank reserves 17 record bits (16 elements and a size bit) and the rewindable bank reserves four, one per element: with 20 types the component reserves **344 bits**, of the 2,048 a replication record allows per entity. Unchanged state emits no update.

| Case | Set property bits | Value/payload bytes |
| --- | ---: | ---: |
| First Float slot | 2 | 13 |
| Full snapshot, 16 Float slots | 17 | 73 |
| One changed Float slot | 1 | 12 |
| One changed Boolean or 8-bit integer | 1 | 9 |
| One changed 16-bit integer | 1 | 10 |
| One changed 64-bit integer | 1 | 16 |
| One changed rewindable Vector3 | 1 | 21 |
| Eight Vector3 RPC arguments | — | 115 |
| Eight full 64-byte String RPC arguments | — | 547 |

Property payloads include the eight-byte schema fingerprint, once per changed component message. Initial vectors also include their size. Rewindable values carry a one-byte type tag instead of a size. RPC arguments and typed inputs carry one tag per value. These figures exclude the replication record, which is shared with the entity's other components, and transport packet headers.

Rewindable properties keep `RewindHistorySize` (128) frames of history per element: four elements of 128 values each, whatever the schema declares. The `Measurements` test prints the bank's size.
