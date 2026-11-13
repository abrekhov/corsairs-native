from datetime import datetime, timezone
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--engine", type=Path, default=ROOT / "build/bin/engine")
parser.add_argument("--cache-mode", type=int, choices=(0, 1, 2), default=0)
args = parser.parse_args()
stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
runtime = ROOT / ".local/save-state-e2e" / stamp
(runtime / "text").mkdir(parents=True)
(runtime / "save").mkdir()
(runtime / "text/program").symlink_to(ROOT / "tests/save-state/program", target_is_directory=True)
config = (ROOT / "config/engine.ini").read_text().replace("run = seadogs.c", "run = roundtrip.c")
config = re.sub(r"(?m)^cache_mode\s*=\s*\d+", f"cache_mode = {args.cache_mode}", config)
(runtime / "engine.ini").write_text(config)
env = os.environ.copy()
env.pop("STORM_INPUT_REPLAY", None)


def run(seconds):
    result = subprocess.run([sys.executable, str(ROOT / "run-native.py"), "--runtime", str(runtime),
                             "--timeout", str(seconds), "--", str(args.engine.resolve())], env=env, timeout=seconds + 15)
    directory = max((runtime / "runs").iterdir())
    state = json.loads((directory / "result.json").read_text())
    logs = (directory / "engine.log").read_text(errors="replace")
    for path in (runtime / "user/Logs").glob("*.log"):
        logs += path.read_text(errors="replace")
    return result.returncode, state, logs


print(f"Runtime: {runtime}", flush=True)
code, state, logs = run(15)
assert code == 0 and state["returncode"] == 0 and "SAVE_E2E_PASS:" in logs and "SAVE_E2E_FAIL:" not in logs and "RUNTIME ERROR" not in logs, logs[-8000:]
save = runtime / "save/attributes-e2e"
assert save.stat().st_size > 32
checksum = hashlib.sha256(save.read_bytes()).hexdigest()
print("PASS: full engine script VM saves and reloads copied trees and references", flush=True)
(runtime / "engine.ini").write_text(config.replace("run = roundtrip.c", "run = interrupted.c"))
code, state, logs = run(4)
assert code == 124 and state.get("timed_out") and "SAVE_E2E_INTERRUPTED:" in logs, state
assert hashlib.sha256(save.read_bytes()).hexdigest() == checksum, "Interrupted save replaced the previous valid save"
print("PASS: independent watchdog interrupts writer; previous save stays byte-identical", flush=True)
