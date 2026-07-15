"""Pre-build script: injects FIRMWARE_VERSION from `git describe` as a macro.

The tag pushed to the repo (e.g. v0.1.0) becomes the firmware version reported
over MAVLink. A `-dirty` suffix flags builds made from an uncommitted tree.
"""

import subprocess

Import("env")

def git_version():
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except Exception:
        return "unknown"

version = git_version()
print(f"  Firmware version: {version}")
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", env.StringifyMacro(version))])
