# Headless MCP multiplayer acceptance

This Windows acceptance uses a separate project, Asset Processor, offscreen Editor,
headless server, and null-renderer client. The agent creates the native schema
through reflected editor bindings, imports it through LocalMcp, and builds the
gameplay graph using the native Script Canvas document/palette/slot APIs. It saves,
reloads, attaches, and cooks those sources before starting the peers.

The graph gathers a Float input from an overridden archetype, processes it on
authority, and sends a reliable autonomous-to-authority RPC with UInt16, Boolean,
and String parameters. Its handler updates authoritative properties. Acceptance
requires the server's RPC message and the client's replicated Health=93,
Ready=true, and input echo=6.5. No game-specific C++ or Python runs in either peer.

The LocalMcp editor authoring and Script Canvas `sc_*` tools are prerequisites.
Their implementation/supporting-change manifest belongs to the LocalMcp change;
the multiplayer acceptance does not implement another graph authoring API.

From a Git LFS-hydrated engine checkout, create a fresh project and configure/build its standard
launchers (use a short engine/build path on Windows):

```powershell
$engine = (Get-Location).Path
$extras = 'C:/o3de-extras' # Matching review branch; LocalMcp lives outside the engine.
$project = 'C:/o3de-multiplayer-proof'
$scripts = "$engine/Gems/Multiplayer/Multiplayer_ScriptCanvas/Code/Tests/Headless"
& "$engine/python/python.cmd" "$scripts/prepare_project.py" --engine $engine --project $project --local-mcp-gem "$extras/Gems/LocalMcp"
cmake -S $engine -B build-script-defined -G 'Visual Studio 17 2022' -A x64 `
  -DLY_PROJECTS=$project -DLY_3RDPARTY_PATH="$env:USERPROFILE/.o3de/3rdParty" -DCMAKE_UNITY_BUILD=ON
cmake --build build-script-defined --config profile --target Editor AssetProcessor AssetBuilder `
  MultiplayerScriptDefinedProof.HeadlessServerLauncher MultiplayerScriptDefinedProof.GameLauncher
& "$engine/python/python.cmd" "$scripts/prove_multiplayer.py" --engine $engine --project $project `
  --output "$engine/build-script-defined/validation/headless-proof"
```

The output directory must be new. The runner refuses occupied ports (Editor MCP
4671, server MCP4672, client MCP4673, AP45670); game traffic uses UDP33481. A Windows
job object owns only this run's hidden processes and closes them, including AP's
builder workers, on success or failure. Existing Editors/APs are never reused.

`result.json` records the assertions and loaded module paths/hashes. `mcp.jsonl`
contains the actual requests/responses. `graph.json` records the reloaded native
nodes/connections; the project retains editable `.mpschema`, `.scriptcanvas`, and
`.prefab` sources. Native stdout and user/log directories remain for diagnostics.
An authored source or unit-test pass alone does not count as loopback acceptance:
`result.json` must contain `"passed": true` and a `network` receipt.

The runner also rejects stale prefab/level products and native prefab export errors.
This matters because the prefab builder can publish an incomplete spawnable after
an editor component logs an export error. The receipt records source/product
hashes and the completed native cook logs.

Verified on 2026-11-05 with Windows profile binaries, Qt offscreen, null rendering,
and separate `Multiplayer.Server.dll` / `Multiplayer.Client.dll` peers. Retained
run: `build-script-defined/validation/headless-proof-12`, with 17 authored nodes,
19 connections, successful reload/attachment, and zero errors/warnings in the
network prefab and level export jobs. The client notifications print `93.0000`,
`true`, and `6.5000`; the server prints the RPC marker and `6.5000`.

The LocalMcp checkpoint used for this run is recorded in
`build-script-defined/validation/localmcp-support-final.json`. Supporting editor,
Script Canvas, and Asset Processor fixes discovered during native acceptance are
separated in `headless-support-fixes.patch` and its adjacent manifest in that
validation directory. Native regressions passed: Multiplayer 200,
Multiplayer_ScriptCanvas 8, PropertyTreeEditor 10, AssetCatalog asset-info 9,
and GraphAutomation 5. This proves the authored scenario on Windows; graphics,
other platforms, packet loss, and high player counts are outside this acceptance.
