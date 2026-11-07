"""Hidden native processes, lifetime-limited to this acceptance run on Windows."""
from __future__ import annotations

import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import subprocess


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class OwnedProcesses:
    def __init__(self, output: Path):
        assert os.name == "nt", "This runner currently supports the Windows Editor build"
        self.output = output
        self.processes = []
        self.streams = []
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.CreateJobObjectW.restype = wintypes.HANDLE
        self.kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        self.kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        self.kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]

        class BasicLimits(ctypes.Structure):
            _fields_ = [("processTime", ctypes.c_int64), ("jobTime", ctypes.c_int64),
                        ("flags", wintypes.DWORD), ("minimum", ctypes.c_size_t), ("maximum", ctypes.c_size_t),
                        ("active", wintypes.DWORD), ("affinity", ctypes.c_size_t),
                        ("priority", wintypes.DWORD), ("scheduling", wintypes.DWORD)]

        class IoCounters(ctypes.Structure):
            _fields_ = [(name, ctypes.c_uint64) for name in ("readOps", "writeOps", "otherOps", "readBytes", "writeBytes", "otherBytes")]

        class ExtendedLimits(ctypes.Structure):
            _fields_ = [("basic", BasicLimits), ("io", IoCounters), ("processMemory", ctypes.c_size_t),
                        ("jobMemory", ctypes.c_size_t), ("peakProcess", ctypes.c_size_t), ("peakJob", ctypes.c_size_t)]

        self.job = self.kernel.CreateJobObjectW(None, None)
        limits = ExtendedLimits()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, including AssetBuilder children.
        if not self.job or not self.kernel.SetInformationJobObject(self.job, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
            raise ctypes.WinError(ctypes.get_last_error())

    def start(self, name, executable, arguments, env=None):
        executable = Path(executable).resolve()
        assert executable.is_file(), executable
        stream = (self.output / f"{name}.stdout.log").open("wb")
        self.streams.append(stream)
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        command = [str(executable), *map(str, arguments)]
        process = subprocess.Popen(command, cwd=executable.parent, env=env, stdin=subprocess.DEVNULL,
                                   stdout=stream, stderr=subprocess.STDOUT, startupinfo=startup,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        if not self.kernel.AssignProcessToJobObject(self.job, wintypes.HANDLE(process._handle)):
            process.kill()
            raise ctypes.WinError(ctypes.get_last_error())
        self.processes.append((name, process))
        with (self.output / "processes.jsonl").open("a", encoding="utf-8") as receipt:
            receipt.write(json.dumps({"name": name, "pid": process.pid, "command": command,
                                      "sha256": sha256(executable)}) + "\n")
        return process

    def modules(self, process, binary):
        class Module(ctypes.Structure):
            _fields_ = [("size", wintypes.DWORD), ("moduleId", wintypes.DWORD), ("processId", wintypes.DWORD),
                        ("globalUsage", wintypes.DWORD), ("processUsage", wintypes.DWORD),
                        ("baseAddress", ctypes.c_void_p), ("baseSize", wintypes.DWORD), ("handle", wintypes.HMODULE),
                        ("name", wintypes.WCHAR * 256), ("path", wintypes.WCHAR * 260)]

        self.kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
        self.kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
        self.kernel.Module32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Module)]
        self.kernel.Module32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Module)]
        snapshot = self.kernel.CreateToolhelp32Snapshot(0x8, process.pid)
        if snapshot == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        modules = []
        try:
            entry = Module()
            entry.size = ctypes.sizeof(entry)
            available = self.kernel.Module32FirstW(snapshot, ctypes.byref(entry))
            assert available, "Cannot enumerate the owned native process modules"
            while available:
                if any(name in entry.name.lower() for name in ("multiplayer", "localmcp", "scriptcanvas", "scriptevents", "aztoolsframework")):
                    path = Path(entry.path).resolve()
                    assert path.is_relative_to(binary.resolve()), ("Foreign feature module loaded", path)
                    modules.append({"path": str(path), "sha256": sha256(path)})
                available = self.kernel.Module32NextW(snapshot, ctypes.byref(entry))
        finally:
            self.kernel.CloseHandle(snapshot)
        assert modules, "Feature modules are missing from the native process"
        return modules

    def close(self):
        if self.job:
            self.kernel.CloseHandle(self.job)
            self.job = None
        for _, process in self.processes:
            process.wait(timeout=30)
        for stream in self.streams:
            stream.close()
