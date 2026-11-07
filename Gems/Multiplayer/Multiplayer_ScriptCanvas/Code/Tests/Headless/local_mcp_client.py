"""Recorded MCP calls for the isolated multiplayer acceptance test."""
from __future__ import annotations

import http.client
import json
import time


class LocalMcp:
    def __init__(self, port, transcript, alive=lambda: None):
        self.port = port
        self.transcript = transcript
        self.alive = alive
        self.sequence = 0

    def request(self, method, params):
        self.sequence += 1
        body = {"jsonrpc": "2.0", "id": self.sequence, "method": method, "params": params}
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=120)
        try:
            connection.request("POST", "/mcp", json.dumps(body), {
                "Content-Type": "application/json", "Accept": "application/json, text/event-stream",
                "MCP-Protocol-Version": "2025-11-25",
            })
            response = connection.getresponse()
            payload = response.read().decode("utf-8")
            assert response.status == 200, (response.status, payload)
            result = json.loads(payload)
            with self.transcript.open("a", encoding="utf-8") as log:
                log.write(json.dumps({"port": self.port, "request": body, "response": result}) + "\n")
            assert "error" not in result, result
            return result["result"]
        finally:
            connection.close()

    def connect(self, timeout=1260):
        deadline = time.monotonic() + timeout
        while True:
            assert self.alive() is None, "Owned engine process exited before MCP became ready"
            try:
                return self.request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                    "clientInfo": {"name": "multiplayer-script-defined-proof", "version": "1"}})
            except (ConnectionError, OSError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.25)

    def call(self, tool, **arguments):
        result = self.request("tools/call", {"name": tool, "arguments": arguments})
        content = result.get("content", [])
        assert len(content) == 1 and content[0]["type"] == "text", result
        data = json.loads(content[0]["text"])
        assert not result.get("isError") and "error" not in data, data
        return data

    def native(self, binding, *args, **keywords):
        value = self.call("binding_call", binding=binding, args=list(args), **keywords)["value"]
        if isinstance(value, dict) and value.get("$type") == "Outcome":
            assert value["success"], value
            value = value["value"]
        return value

    def console(self, command):
        return self.call("console_execute", command=command)
