"""Create the isolated native-launcher project used by prove_multiplayer.py."""
import argparse
import json
from pathlib import Path
import shutil


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--engine", type=Path, required=True)
parser.add_argument("--project", type=Path, required=True)
parser.add_argument("--local-mcp-gem", type=Path, required=True, help="Path to o3de-extras/Gems/LocalMcp")
args = parser.parse_args()
local_mcp = args.local_mcp_gem.resolve()
if not (local_mcp / "gem.json").is_file() or json.loads((local_mcp / "gem.json").read_text(encoding="utf-8"))["gem_name"] != "LocalMcp":
    parser.error("--local-mcp-gem must name the LocalMcp gem in o3de-extras")
project = args.project.resolve()
project.mkdir(parents=True, exist_ok=False)
(project / "project.json").write_text(json.dumps({
    "project_name": "MultiplayerScriptDefinedProof",
    "product_name": "Multiplayer Script Defined Headless Proof",
    "project_id": "{CF7AD040-2063-4B95-B051-CA6FA1592A92}",
    "version": "1.0.0", "engine": "o3de", "script_only": True,
    "external_subdirectories": [str(local_mcp)],
    "gem_names": ["Atom", "Camera", "CameraFramework", "CertificateManager", "Compression",
                  "EditorPythonBindings", "ImGui", "LmbrCentral", "LocalMcp", "Multiplayer",
                  "Multiplayer_ScriptCanvas", "PhysX5", "PrefabBuilder", "PrimitiveAssets", "ScriptCanvas", "ScriptEvents"],
}, indent=2), encoding="utf-8")
(project / "CMakeLists.txt").write_text("# Uses standard native launchers; no game-specific C++ target.\n", encoding="utf-8")
(project / "Registry").mkdir()
template = args.engine.resolve() / "Templates/ScriptOnlyProject/Template"
for directory in ("ShaderLib", "Config"):
    shutil.copytree(template / directory, project / directory)
shutil.copyfile(template / "Registry/assetprocessor_settings.setreg", project / "Registry/assetprocessor_settings.setreg")
(project / "Registry/localmcp.setreg").write_text(json.dumps({
    "O3DE": {"Gems": {"LocalMcp": {"Enabled": False, "Editor": {
        "Enabled": True, "Port": 4671, "IdleTimeoutMs": 120000, "LogLines": 16384,
    }}}},
}, indent=2), encoding="utf-8")
print(project)
