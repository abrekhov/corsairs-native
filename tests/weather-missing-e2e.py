import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--engine", type=Path, default=ROOT / "build/bin/engine")
args = parser.parse_args()
engine = args.engine.resolve()
stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
runtime = ROOT / ".local/weather-missing-e2e" / stamp
runtime.mkdir(parents=True)
(runtime / "text").mkdir()
(runtime / "text/program").symlink_to(ROOT / "tests/weather-missing/program", target_is_directory=True)
config = (ROOT / "config/engine.ini").read_text().replace("run = seadogs.c", "run = weather-missing.c")
(runtime / "engine.ini").write_bytes(config.encode())

if subprocess.run(["pgrep", "-f", "(^|/)engine(-1)?$"], capture_output=True).returncode == 0:
    raise SystemExit("Game already running; test refused")

result = subprocess.run([sys.executable, str(ROOT / "run-native.py"), "--runtime", str(runtime),
                         "--timeout", "20", "--", str(engine)], capture_output=True, text=True, timeout=40)
log = (sorted((runtime / "runs").glob("*"))[-1] / "engine.log").read_text(errors="replace")
system_log = (runtime / "user/Logs/system.log").read_text(errors="replace")
assert "WEATHER-MISSING-OK" in log, f"Test script did not finish:\n{log[-3000:]}"
assert "terminating due to uncaught exception" not in result.stderr, result.stderr[-3000:]
assert "libc++abi" not in result.stderr, result.stderr[-3000:]
assert result.returncode == 0, f"returncode={result.returncode}\n{result.stderr[-2000:]}"
assert "weather entity not found yet" in system_log, "Expected graceful weather-missing traces"
print(f"PASS: engine exits cleanly when rain/sunglow/waterflare are created without weather ({stamp})")
