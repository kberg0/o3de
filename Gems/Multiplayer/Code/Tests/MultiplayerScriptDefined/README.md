# Script-defined multiplayer validation

Authoring and Script Canvas usage are described in the [integration guide](../../../Multiplayer_ScriptCanvas/README.md).

Runtime code belongs to Multiplayer: shared definitions/wire types in `Multiplayer.Common.Static`, and generated components in the client/server/unified targets. Only fixtures and measurements belong to the test target.

## Review order

1. `Source/AutoGen/MultiplayerScriptDefined*.AutoComponent.xml`: 16 typed property banks using `Container="Vector"`, eight compiled RPC route/reliability endpoints, and the optional input companion.
2. `MultiplayerScriptDefinedTypes`: bounded tagged values and transactional input/RPC decoding. Every enum tag has a compile-time assertion against its variant alternative.
3. `MultiplayerScriptDefinedDefinition`: typed defaults, bounds, stable IDs/slot mappings, and locale-independent schema fingerprints.
4. `MultiplayerScriptDefinedComponent`: defaults before attachment, populated-slot dirty marking before controllers exist, checked writes/RPC dispatch, and schema checks before property deserialization.
5. `MultiplayerScriptDefinedInputComponent`: immutable input layout, gather-only setters, input processing and replay.
6. `MultiplayerScriptDefinedComponentTests.cpp`: native integration, malformed input/RPC records, all RPC routes, reflection with EditContext, schema mismatch rejection, and measurements.

Both native components register their concrete SerializeContext class before calling generated base reflection. The base reflects the concrete class into EditContext; reversing this order crashes tool module loading.

## Core support

- `MultiplayerController::IsInputEnabled()` defaults to true. NetBind evaluates it while rebuilding input ordering, so omitted script-defined inputs add no frame/callbacks.
- Ownership updates visit every serialization component, including opted-out components and proxies.
- Generated RPC invocations stop when `SetRpcParams` fails.
- Initialization helpers are opt-in per XML component and reject changes after attachment.

## Reproduce in this worktree

```powershell
$stateFix = Join-Path $PWD "build-script-defined/ShortBuildState.targets"
'<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup><LastBuildState>$(TLogLocation)state</LastBuildState></PropertyGroup></Project>' | Set-Content $stateFix
cmake --build build-script-defined --config profile --target Multiplayer.Tests Multiplayer_ScriptCanvas.Tests Multiplayer_ScriptCanvas.Editor Multiplayer.Client Multiplayer.Server ScriptEvents.Editor AzTestRunner -- /m:1 /p:CL_MPCount=4 /p:ForceImportAfterCppTargets=$stateFix
Push-Location build-script-defined/bin/profile
.\AzTestRunner.exe .\Multiplayer.Tests.dll AzRunUnitTests
.\AzTestRunner.exe .\Multiplayer_ScriptCanvas.Tests.dll AzRunUnitTests
Pop-Location
```

The native measurement tests print CSV-style object sizes, record bits and bytes, initial/growth traffic, and single-element deltas. Changed property payloads now include an eight-byte schema fingerprint. Empty state has no fingerprint/value payload. The generated vectors reserve capacity inline and reserve capacity + 1 dirty bits per bank, even when empty. Only omitting the input companion has zero incremental input cost.

## Current native measurements

Measured on Windows x64, MSVC 19.44.35227, profile, on 2026-11-05. Serializer type validation is disabled for byte measurements; another test exercises validated RPC dispatch.

| Object | Bytes |
| --- | ---: |
| Generated state component / concrete runtime component | 2,864 / 3,248 |
| State controller / generated component record | 16 / 24 |
| Generated input component / concrete runtime input component | 104 / 424 |
| Input controller / allocated component input frame | 16 / 96 |
| RPC value / bounded payload / signature | 96 / 816 / 14 |
| Existing shared ReplicationRecord | 1,144 |

These are object sizes, not allocation-inclusive per-entity totals. Definitions, RPC signatures, and bridge metadata have additional dynamic allocations. Typed input frames allocate a vector only for used values: each capacity element adds 96 bytes, excluding allocator overhead. Omitting the input companion removes both the frame and that allocation.

A capacity-16 Float bank occupies 68 bytes even when empty. Boolean/8-bit integer banks occupy 17 bytes each, 16-bit integer banks 34, 32-bit banks 68, and 64-bit banks 136. Each of the 16 banks reserves 17 dirty bits; the component reserves **272 bits** and its explicitly serialized entity record occupies **36 bytes**. Normal unchanged state emits no update.

| Case | Set property bits | Value/payload bytes | Record/envelope bytes |
| --- | ---: | ---: | ---: |
| First Float slot | 2 | 13 | 36 |
| Full snapshot, 16 Float slots | 17 | 73 | 36 |
| One changed Float slot | 1 | 12 | 36 |
| One changed Boolean or 8-bit integer | 1 | 9 | 36 |
| One changed 16-bit integer | 1 | 10 | 36 |
| One changed 64-bit integer | 1 | 16 | 36 |
| Eight Vector3 RPC arguments | — | 115 | 17 |
| Eight full 64-byte String RPC arguments | — | 547 | 17 |

Property payloads above include the eight-byte schema fingerprint, once per changed component message. Initial vectors also include their size. The record is shared with the entity's other components, so the combined figures describe the test fixture, not a fixed packet overhead in every game.

A bare NetworkInput is 20 bytes in the fixture. Adding the legacy two-Float layout produces 40 bytes. Changing one of those input values creates five scalar delta bits, four value-buffer bytes, and a 10-byte serialized SerializerDelta. Typed inputs additionally carry one tag per used value; wire size follows the declared types. These measurements exclude transport packet headers.

The fresh worktree path can exceed MSBuild's legacy path limit for its state filename. The local validation build uses a ForceImportAfterCppTargets file that shortens LastBuildState; that workaround is a build artifact, not an engine change. Full Editor targets also have long generated Qt paths; use a shorter checkout path for those targets. The local broad build uses temporary short paths in generated projects/unity files.

Validation: all 199 tests in Multiplayer.Tests passed, including 34 script-defined component tests. All seven Multiplayer_ScriptCanvas.Tests passed, including schema asset save/build, optional input export, all 16 pin types, and real Script Events request/notification calls.
