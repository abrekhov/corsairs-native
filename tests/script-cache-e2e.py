from datetime import datetime, timezone
import argparse
import os
import re
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--mode", type=int, choices=(1, 2), default=1)
args = parser.parse_args()
assert subprocess.run(["pgrep", "-x", "engine|engine-1|focus-e2e"], capture_output=True).returncode == 1, "Game/probe already running"
runtime = ROOT / ".local/script-cache-e2e" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
program = runtime / "text/program"
program.mkdir(parents=True)
source = program / "cache-main.c"
source.write_text('''#define CACHE_SCALE 0.123456789
#define CACHE_VERSION "original"
object Render;
float initialized = 0.123456789;
int count = 7;
string label = "cache-value";
extern void ValidateLate();
void Main()
{
    SetEventHandler("Validate", "Validate", 0);
    PostEvent("Validate", 100);
}
void Validate()
{
    LoadSegment("cache-late.c");
    ValidateLate();
}
''')
late = program / "cache-late.c"
late.write_text('''void ValidateLate()
{
    if (CACHE_SCALE != 0.123456789 || initialized != 0.123456789 || count != 7 || label != "cache-value")
    {
        Trace("CACHE_E2E_FAIL: cached constants/initial values");
    }
    else
    {
        Trace("CACHE_E2E_PASS: " + CACHE_VERSION);
    }
    ExitProgram();
}
''')
config = (ROOT / "config/engine.ini").read_text().replace("run = seadogs.c", "run = cache-main.c")
config = re.sub(r"(?m)^cache_mode\s*=\s*\d+", f"cache_mode = {args.mode}", config)
(runtime / "engine.ini").write_text(config)
env = os.environ.copy()
env.pop("STORM_INPUT_REPLAY", None)


def run(label, expected):
    result = subprocess.run([sys.executable, str(ROOT / "run-native.py"), "--runtime", str(runtime), "--timeout", "12"], env=env, timeout=25)
    directory = max((runtime / "runs").iterdir())
    logs = "\n".join(p.read_text(errors="replace") for p in (runtime / "user/Logs").glob("*.log"))
    assert result.returncode == 0 and f"CACHE_E2E_PASS: {expected}" in logs and "CACHE_E2E_FAIL" not in logs and "RUNTIME ERROR" not in logs, f"{label}: {directory}\n{logs[-6000:]}"
    print(f"PASS: {label}: {directory}", flush=True)
    return directory


print(f"Runtime: {runtime}", flush=True)
run("cold cache compile", "original")
cache = runtime / "user/Cache"
binaries = list(cache.rglob("*.b"))
assert len(binaries) >= 2
late_cache = next(p for p in binaries if "cache-late" in p.name)
late_cache.unlink()
before = {p: p.stat().st_mtime_ns for p in binaries if p != late_cache}
run("warm main cache with newly compiled late segment", "original")
assert before == {p: p.stat().st_mtime_ns for p in before}, "Warm execution rewrote/recompiled main cache"
stat = source.stat()
source.write_text(source.read_text().replace('"original"', '"modified"'))
os.utime(source, ns=(stat.st_atime_ns, stat.st_mtime_ns))
run("source edit preserving mtime and size", "modified")
run("modified warm cache execution", "modified")
main_cache = next(p for p in cache.rglob("*.b") if "cache-main" in p.name)
main_cache.write_bytes(main_cache.read_bytes()[:11])
run("truncated cache recompiles safely", "modified")
data = bytearray(main_cache.read_bytes())
data[-1] ^= 1
main_cache.write_bytes(data)
run("corrupted cache checksum recompiles safely", "modified")
data = bytearray(main_cache.read_bytes())
data[0] ^= 1
main_cache.write_bytes(data)
run("old cache version recompiles safely", "modified")
previous = main_cache.stat().st_mtime_ns
(runtime / "engine.ini").write_text(re.sub(r"(?m)^debuginfo\s*=\s*\d+", "debuginfo = 0", config))
run("debug bytecode setting invalidates cache", "modified")
assert main_cache.stat().st_mtime_ns != previous
(runtime / "engine.ini").write_text(config)
run("debug bytecode setting restored", "modified")
print(f"All script cache E2E scenarios passed: {runtime}", flush=True)
