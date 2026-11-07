"""Author a network pawn through LocalMcp, cook it, and run native loopback peers.

No game-specific C++ or Python runs inside the peers: their gameplay is the
Script Canvas graph created by this MCP client. All native calls are recorded.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import socket
import time
import traceback
import uuid

from agent_scenario import SCHEMA, NODES, CONSTANTS, CONNECTIONS
from local_mcp_client import LocalMcp
from owned_processes import OwnedProcesses, sha256


def eventually(action, predicate, timeout=120):
    end = time.monotonic() + timeout
    while True:
        result = action()
        if predicate(result):
            return result
        assert time.monotonic() < end, repr(result)[-4000:]
        time.sleep(0.25)


def tagged(kind, value):
    return {"$type": kind, "value": value}


def slot(node, semantic, direction):
    visual = node.get("createdGraphCanvas", node["graphCanvas"])
    slots = [s for s in node["slots"] if s["direction"] == direction and s["graphCanvasNode"] == visual]
    name = semantic.rsplit(".", 1)[-1]
    found = [s for s in slots if s["name"] in (semantic, name)]
    if not found:
        found = [s for s in slots if s["kind"] == "execution" and s["name"].endswith(semantic)]
    if not found and name == "Result":
        found = [s for s in slots if s["kind"] == "data"]
    assert len(found) == 1, (semantic, direction, node["id"], [(s["name"], s["id"]) for s in slots])
    return found[0]["id"]


def property_path(mcp, component, suffix):
    properties = mcp.call("component_properties", component=component)["properties"]
    matches = [p["path"] for p in properties if p["path"] == suffix or p["path"].endswith("|" + suffix)]
    assert len(matches) == 1, (suffix, properties)
    return matches[0]


def set_property(mcp, component, suffix, value):
    path = property_path(mcp, component, suffix)
    mcp.call("component_set", component=component, path=path, value=value)
    return mcp.call("component_get", component=component, path=path)["value"]


def cooked_prefab(project, source, product):
    """Reject stale products and native export errors, even if AP publishes a product."""
    cache = project / "Cache/pc" / product
    eventually(lambda: cache.is_file() and cache.stat().st_mtime_ns >= source.stat().st_mtime_ns, bool, timeout=180)
    folder = project / "user/log/JobLogs" / source.relative_to(project).parent

    def completed_logs():
        return [(path, path.read_text(encoding="utf-8", errors="replace"))
                for path in folder.glob(source.name + "-*.log")]

    logs = eventually(completed_logs, lambda entries: any(
        "task (process)" in text and re.search(r"S: \d+ errors, \d+ warnings", text) for _, text in entries))
    records = []
    for path, text in logs:
        if "task (process)" in text:
            counts = re.findall(r"S: (\d+) errors, (\d+) warnings", text)
            assert counts and all(int(errors) == 0 for errors, _ in counts), (str(path), counts, text[-4000:])
            records.append({"path": str(path), "sha256": sha256(path), "diagnostics": counts})
    return {"source": str(source), "sourceSha256": sha256(source), "product": str(cache),
            "productSha256": sha256(cache), "jobs": records}


def author(mcp, project, output, run):
    incoming = f"user/proof_incoming/{run}/actor.mpschema"
    mcp.native("global/method/ScriptDefined_SaveSchema", incoming, json.dumps(SCHEMA), False)
    saved = json.loads(mcp.native("global/method/ScriptDefined_ReadSchema", incoming))
    assert saved["Name"] == SCHEMA["Name"] and saved["Inputs"][0]["Name"] == "Move", saved
    imported = mcp.call("asset_import", source_path=str(project / incoming),
                        dest_path=f"Assets/MultiplayerProof/{run}/actor.mpschema", timeout_ms=110000)
    (output / "schema-import.json").write_text(json.dumps(imported, indent=2), encoding="utf-8")

    level = f"Levels/McpProof_{run}"
    level_source = project / level / (Path(level).name + ".prefab")
    mcp.call("level_create", path=str(level_source))
    type_info = mcp.call("component_types")
    types = dict(zip(type_info["names"], type_info["types"]))
    (output / "component-types.json").write_text(json.dumps(types, indent=2), encoding="utf-8")

    graph_path = str(project / f"Assets/MultiplayerProof/{run}/actor.scriptcanvas")
    graph = mcp.call("sc_create", path=graph_path)["graph"]
    initial_graph = mcp.call("sc_describe", graph=graph)
    bus_name = SCHEMA["Name"] + "_" + re.sub(r"[{}-]", "", imported["sourceUuid"])
    nodes = {}
    for index, (alias, (kind, event)) in enumerate(NODES.items()):
        query = event if kind == "node" else bus_name + "/" + event
        palette = eventually(lambda: mcp.call("sc_palette", query=query, limit=256),
                             lambda p: any(e["name"] == event for e in p["entries"]))
        choices = [e for e in palette["entries"] if e["name"] == event]
        if kind != "node":
            factories = ("receive", "handler") if kind == "receive" else ("send",)
            choices = [e for e in choices if any(factory in e["factory"].lower() for factory in factories)]
        assert len(choices) == 1, (alias, choices)
        nodes[alias] = mcp.call("sc_add_node", graph=graph, palette=choices[0]["id"],
                               position=tagged("Vector2", [index % 5 * 360, index // 5 * 380]))
    for alias, values in CONSTANTS.items():
        for name, value in values.items():
            mcp.call("sc_set_value", graph=graph, node=nodes[alias]["id"],
                     slot=slot(nodes[alias], name, "in"), value=value)
    for source, source_slot, target, target_slot in CONNECTIONS:
        mcp.call("sc_connect", graph=graph, source_node=nodes[source]["id"],
                 source_slot=slot(nodes[source], source_slot, "out"), target_node=nodes[target]["id"],
                 target_slot=slot(nodes[target], target_slot, "in"))
    mcp.call("sc_save", graph=graph, path=graph_path)
    reloaded = mcp.call("sc_reload", graph=graph)
    description = mcp.call("sc_describe", graph=reloaded["graph"])
    assert len(description["nodes"]) == len(initial_graph["nodes"]) + len(nodes), description
    assert len(description["connections"]) == len(initial_graph["connections"]) + len(CONNECTIONS), description
    (output / "graph.json").write_text(json.dumps(description, indent=2), encoding="utf-8")
    print("Native graph saved, cooked and reloaded", flush=True)

    pawn = mcp.call("entity_create", name="McpProofAvatar")["entity"]
    required = ["Network Binding", "Multiplayer Script Defined", "Local Prediction Player Input", "Script Canvas"]
    for name in required:
        assert name in types, (name, sorted(types))
    components = mcp.call("component_add", entity=pawn, types=[types[name] for name in required])["components"]
    assert len(components) == len(required), components
    components = dict(zip(required, components))
    # Resolve the imported native definition product, retaining its catalog sub-ID.
    products = imported["products"]
    definition = next(p["id"] for p in products if p["id"]["subId"] == 0)
    assert set_property(mcp, components["Multiplayer Script Defined"], "Definition", definition) == definition
    eventually(lambda: mcp.call("component_properties", component=components["Multiplayer Script Defined"]),
               lambda p: any(entry["path"].endswith("Override default") for entry in p["properties"]))
    assert set_property(mcp, components["Multiplayer Script Defined"], "Override default", True) is True
    assert set_property(mcp, components["Multiplayer Script Defined"], "Value", "6.5") == "6.5"
    # The prefab override is exercised by graph GetArchetype_Step -> typed input.
    assert mcp.call("sc_attach", component=components["Script Canvas"], path=graph_path)
    attachment = mcp.call("sc_attachment", component=components["Script Canvas"])
    assert attachment["selected"] and Path(attachment["path"]).resolve() == Path(graph_path).resolve(), attachment
    assert attachment["sourceUuid"] == description["sourceUuid"], attachment
    (output / "attachment.json").write_text(json.dumps(attachment, indent=2), encoding="utf-8")
    print("Component definition, archetype override and graph attachment read back", flush=True)
    prefab = str(project / f"Prefabs/MultiplayerProof/{run}/Avatar.prefab")
    Path(prefab).parent.mkdir(parents=True, exist_ok=False)
    container = mcp.call("prefab_create", entities=[pawn], path=prefab)["container"]
    mcp.call("prefab_save", path=Path(prefab).relative_to(project).as_posix())
    # This instance is a source for the spawner, not an extra map pawn.
    mcp.call("entity_delete", entities=[container])

    spawn_point = mcp.call("entity_create", name="NetworkSpawnPoint")["entity"]
    spawner_type = mcp.native("bus/EditorComponentAPIBus/event/FindComponentTypeIdsByEntityType",
                              ["Simple Network Player Spawner"], 3)
    assert len(spawner_type) == 1, spawner_type
    spawner_component = mcp.native("bus/EditorLevelComponentAPIBus/event/AddComponentsOfType", spawner_type)[0]
    spawnable_path = Path(prefab).relative_to(project).with_suffix(".network.spawnable").as_posix().lower()
    asset = eventually(lambda: mcp.native("bus/AssetCatalogRequestBus/event/GetAssetIdByPath", spawnable_path,
                                         tagged("Uuid", "{00000000-0000-0000-0000-000000000000}"), False),
                       lambda a: a.get("guid", "").strip("{}0-") != "", timeout=180)
    prefab_cook = cooked_prefab(project, Path(prefab), spawnable_path)
    print("Network prefab exported and cooked", flush=True)
    # The existing simple spawner requires a nonempty spawn point list at join.
    tree = mcp.native("bus/EditorComponentAPIBus/event/BuildComponentPropertyTreeEditor", spawner_component)
    try:
        mcp.native("class/PropertyTreeEditor/method/AppendContainerItem",
                   property_path(mcp, spawner_component, "Spawn Points"), spawn_point, target=tree)
    finally:
        mcp.call("binding_release", handles=[tree["$handle"]])
    # The component API marks the edited entity dirty for the native level save.
    set_property(mcp, spawner_component, "Network Spawnable Asset", asset)
    mcp.call("level_save")
    assert level_source.is_file(), level_source
    map_asset = level_source.relative_to(project).with_suffix(".spawnable").as_posix().lower()
    level_cook = cooked_prefab(project, level_source, map_asset)
    return {"level": str(level_source), "map": map_asset, "prefab": prefab, "graph": graph_path,
            "definition": definition, "expected": {"Health": 93, "Ready": True, "LastMove": 6.5},
            "cooks": {"prefab": prefab_cook, "level": level_cook}}


def registry(key, value):
    return f"--regset={key}={str(value).lower() if isinstance(value, bool) else value}"


def bootstrap(engine, project, user, ap_port):
    return [f"--engine-path={engine}", f"--project-path={project}", "--NullRenderer", "--rhi=null", "-platform", "offscreen",
            registry("/Amazon/AzCore/Bootstrap/project_user_path", user),
            registry("/Amazon/AzCore/Bootstrap/remote_port", ap_port)]


def runtime_flags(port):
    return [registry("/O3DE/Gems/LocalMcp/Enabled", True), registry("/O3DE/Gems/LocalMcp/Port", port),
            registry("/O3DE/Gems/LocalMcp/Auth", "none"),
            registry("/O3DE/Gems/LocalMcp/IdleTimeoutMs", 120000), registry("/O3DE/Gems/LocalMcp/LogLines", 16384),
            registry("/O3DE/Gems/LocalMcp/Editor/Enabled", False),
            registry("/O3DE/Autoexec/ConsoleCommands/net_UdpUseEncryption", False)]


class LogReader:
    def __init__(self, mcp, destination):
        self.mcp = mcp
        self.destination = destination
        self.cursor = 0
        self.messages = []

    def read(self):
        result = self.mcp.call("logs_tail", after=self.cursor, limit=128)
        assert result["dropped"] == 0, "Native logs were overwritten before acceptance could inspect them"
        self.cursor = result["next"]
        self.messages.extend(line["message"] for line in result["lines"])
        text = "\n".join(self.messages)
        self.destination.write_text(text, encoding="utf-8")
        return text


def network(owned, engine, binary, project, output, env, authored, ap_port):
    arguments = bootstrap(engine, project, output / "server_user", ap_port) + runtime_flags(4672)
    arguments += [registry("/Amazon/AzCore/Bootstrap/assets", "pc"),
                  registry("/O3DE/Autoexec/ConsoleCommands/sv_port", 33481),
                  registry("/O3DE/Autoexec/ConsoleCommands/sv_portRange", 0),
                  registry("/O3DE/Autoexec/ConsoleCommands/sv_terminateOnPlayerExit", False),
                  registry("/O3DE/Autoexec/ConsoleCommands/sv_dedicated_host_onstartup", False)]
    server_process = owned.start("server", binary / "MultiplayerScriptDefinedProof.HeadlessServerLauncher.exe", arguments, env)
    server = LocalMcp(4672, output / "mcp.jsonl", server_process.poll)
    server.connect()
    server_modules = owned.modules(server_process, binary)
    for command in ("sv_dedicated_host_onstartup false", "sv_terminateOnPlayerExit false", "sv_portRange 0", "sv_port 33481",
                    "sv_isDedicated true", "sv_map " + authored["map"], "host", "loadlevel " + authored["map"]):
        server.console(command)
    client_process = owned.start("client", binary / "MultiplayerScriptDefinedProof.GameLauncher.exe",
                                 bootstrap(engine, project, output / "client_user", ap_port) + runtime_flags(4673), env)
    client = LocalMcp(4673, output / "mcp.jsonl", client_process.poll)
    client.connect()
    client_modules = owned.modules(client_process, binary)
    for command in ("cl_serveraddr 127.0.0.1", "cl_serverport 33481", "connect"):
        client.console(command)
    server_logs = LogReader(server, output / "server-mcp.log")
    client_logs = LogReader(client, output / "client-mcp.log")
    eventually(lambda: (server_logs.read(), client_logs.read()),
               lambda pair: re.search(r"(?m)^\s*MCP_MULTIPLAYER_RPC_RECEIVED\s*$", pair[0])
               and re.search(r"(?m)^\s*6\.50*\s*$", pair[0])
               and re.search(r"(?m)^\s*93(?:\.0+)?\s*$", pair[1])
               and re.search(r"(?m)^\s*6\.50*\s*$", pair[1])
               and re.search(r"(?im)^\s*true\s*$", pair[1]), timeout=180)
    assert server_process.poll() is None and client_process.poll() is None
    return {"serverRpc": True, "clientHealth": 93, "clientReady": True, "clientInputEcho": 6.5,
            "serverPid": server_process.pid, "clientPid": client_process.pid,
            "serverModules": server_modules, "clientModules": client_modules}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--author-only", action="store_true")
    args = parser.parse_args()
    engine, project, output = args.engine.resolve(), args.project.resolve(), args.output.resolve()
    binary = args.binary.resolve() if args.binary else engine / "build-script-defined/bin/profile"
    output.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", LANG="C")
    cache = (binary.parent.parent / "CMakeCache.txt").read_text(encoding="utf-8")
    qt = next(line.split("=", 1)[1] for line in cache.splitlines() if line.startswith("QT_PATH:"))
    env["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(Path(qt) / "plugins/platforms")
    for port in (4671, 4672, 4673, 45670):
        with socket.socket() as test:
            test.bind(("127.0.0.1", port))
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as test:
        test.bind(("127.0.0.1", 33481))
    owned = OwnedProcesses(output)
    receipt = {"passed": False, "engine": str(engine), "project": str(project)}
    try:
        ap_port = 45670
        ap_process = owned.start("asset-processor", binary / "AssetProcessor.exe",
                    [f"--engine-path={engine}", f"--project-path={project}", "--start-hidden", "-platform", "offscreen",
                     registry("/Amazon/AzCore/Bootstrap/remote_port", ap_port)], env)
        print("Waiting for the standard scene/view shader bootstrap asset", flush=True)
        def bootstrap_ready():
            assert ap_process.poll() is None, "Owned Asset Processor exited during bootstrap cook"
            return (project / "Cache/pc/shaders/sceneandviewsrgs.azshader").is_file()

        eventually(bootstrap_ready, bool, timeout=1260)
        editor_process = owned.start("editor", binary / "Editor.exe",
                                     bootstrap(engine, project, output / "editor_user", ap_port) + ["--BatchMode", "--autotest_mode",
                                     "--skipWelcomeScreenDialog", registry("/O3DE/Gems/LocalMcp/Enabled", False),
                                     registry("/O3DE/Gems/LocalMcp/Editor/Enabled", True),
                                     registry("/O3DE/Gems/LocalMcp/Editor/Port", 4671),
                                     registry("/O3DE/Gems/LocalMcp/Editor/Auth", "none"),
                                     registry("/O3DE/Gems/LocalMcp/Editor/IdleTimeoutMs", 120000)], env)
        def editor_alive():
            assert ap_process.poll() is None, "Owned Asset Processor exited during startup"
            return editor_process.poll()

        editor = LocalMcp(4671, output / "mcp.jsonl", editor_alive)
        print("Waiting for isolated offscreen Editor MCP", flush=True)
        editor.connect()
        receipt["editorModules"] = owned.modules(editor_process, binary)
        run = uuid.uuid4().hex[:8]
        receipt["authored"] = author(editor, project, output, run)
        print("MCP-authored assets saved, reloaded, attached and cooked", flush=True)
        if not args.author_only:
            receipt["network"] = network(owned, engine, binary, project, output, env, receipt["authored"], ap_port)
            receipt["passed"] = True
            print("Native loopback input/RPC/replication assertions passed", flush=True)
    except BaseException:
        receipt["error"] = traceback.format_exc()
        receipt["processExitCodes"] = {name: process.poll() for name, process in owned.processes}
        raise
    finally:
        (output / "result.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
        owned.close()


if __name__ == "__main__":
    main()
