"""Declarative gameplay authored by the MCP acceptance test; no native gameplay code."""
SCHEMA = {
    "Name": "McpProofAvatar",
    "Properties": [
        {"Name": "Health", "Type": 7, "Default": "100"},
        {"Name": "Ready", "Type": 0, "Default": "false"},
        {"Name": "LastMove", "Type": 2, "Default": "0"},
    ],
    "Inputs": [{"Name": "Move", "Type": 2, "Default": "0"}],
    "Archetypes": [{"Name": "Step", "Type": 2, "Default": "3.25"}],
    "Rpcs": [{"Name": "Initialize", "Delivery": 3, "Reliable": True, "Parameters": [
        {"Name": "Health", "Type": 7}, {"Name": "Ready", "Type": 0}, {"Name": "Message", "Type": 14},
    ]}],
}

# Event receivers and calls are standard Script Events palette entries. The agent
# discovers their native node/slot IDs before connecting them through LocalMcp.
NODES = {
    "gather": ("receive", "OnCreateInput"),
    "step": ("send", "GetArchetype_Step"),
    "write_input": ("send", "SetInput_Move"),
    "process": ("receive", "OnProcessInput"),
    "read_input": ("send", "GetInput_Move"),
    "last_move": ("send", "Set_LastMove"),
    "send_rpc": ("send", "Send_Initialize"),
    "receive_rpc": ("receive", "OnRpc_Initialize"),
    "health": ("send", "Set_Health"),
    "ready": ("send", "Set_Ready"),
    "rpc_print": ("node", "Print"),
    "health_change": ("receive", "On_Health"),
    "health_print": ("node", "Print"),
    "move_change": ("receive", "On_LastMove"),
    "move_print": ("node", "Print"),
    "ready_change": ("receive", "On_Ready"),
    "ready_print": ("node", "Print"),
}

CONSTANTS = {"send_rpc": {"Health": 93.0, "Ready": True, "Message": "MCP_MULTIPLAYER_RPC_RECEIVED"}}

CONNECTIONS = [
    ("gather", "OnCreateInput", "step", "In"),
    ("step", "Out", "write_input", "In"),
    ("step", "Result", "write_input", "Value"),
    ("process", "OnProcessInput", "read_input", "In"),
    ("read_input", "Out", "last_move", "In"),
    ("read_input", "Result", "last_move", "Value"),
    ("last_move", "Out", "send_rpc", "In"),
    ("receive_rpc", "OnRpc_Initialize", "health", "In"),
    ("receive_rpc", "OnRpc_Initialize.Health", "health", "Value"),
    ("health", "Out", "ready", "In"),
    ("receive_rpc", "OnRpc_Initialize.Ready", "ready", "Value"),
    ("ready", "Out", "rpc_print", "In"),
    ("receive_rpc", "OnRpc_Initialize.Message", "rpc_print", "Value"),
    ("health_change", "On_Health", "health_print", "In"),
    ("health_change", "On_Health.Value", "health_print", "Value"),
    ("move_change", "On_LastMove", "move_print", "In"),
    ("move_change", "On_LastMove.Value", "move_print", "Value"),
    ("ready_change", "On_Ready", "ready_print", "In"),
    ("ready_change", "On_Ready.Value", "ready_print", "Value"),
]
