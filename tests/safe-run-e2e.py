import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "run-native.py"
STOP = "import os,signal; print('stopped', flush=True); os.kill(os.getpid(), signal.SIGSTOP)"


def command(runtime, seconds, code):
    return [sys.executable, str(RUNNER), "--runtime", str(runtime), "--timeout", str(seconds),
            "--", sys.executable, "-c", code]


def await_file(runtime, name, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        found = sorted(runtime.glob(f"runs/*/{name}"))
        if found:
            return found[0]
        time.sleep(0.05)
    raise AssertionError(f"Missing {name} under {runtime}")


def gone(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    return False


with tempfile.TemporaryDirectory(prefix="coas-safe-e2e-") as temporary:
    base = Path(temporary)
    env_code = "import os; from pathlib import Path; print(os.getcwd()); print(os.environ['STORM_USER_PATH']); print(os.environ['STORM_SAFE_WINDOW']); print(Path('PROGRAM/seadogs.c').exists())"
    runtime = base / "normal"
    run = subprocess.run(command(runtime, 3, env_code), capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, run.stdout + run.stderr
    result_path = await_file(runtime, "result.json")
    result = json.loads(result_path.read_text())
    log = (result_path.parent / "engine.log").read_text()
    assert str(runtime / "user") in log and "True" in log
    assert not result["timed_out"] and gone(result["pid"])
    print("PASS: normal exit, isolated config/assets/state, child reaped")

    sibling = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
    try:
        runtime = base / "stopped"
        run = subprocess.run(command(runtime, 0.5, STOP), capture_output=True, text=True, timeout=10)
        result = json.loads(await_file(runtime, "result.json").read_text())
        assert run.returncode == 124, run.stdout + run.stderr
        assert result["timed_out"] and result["returncode"] == -signal.SIGKILL
        assert gone(result["pid"]) and sibling.poll() is None
        print("PASS: SIGSTOP hang killed at deadline, unrelated process untouched")

        runtime = base / "orphan"
        caller = subprocess.Popen(command(runtime, 2, STOP), stdout=subprocess.DEVNULL)
        watchdog_path = await_file(runtime, "watchdog.json")
        watchdog = json.loads(watchdog_path.read_text())
        caller.kill()
        caller.wait(timeout=2)
        assert not gone(watchdog["watchdog_pid"])
        result = json.loads(await_file(runtime, "result.json").read_text())
        assert result["timed_out"] and gone(result["pid"]) and sibling.poll() is None
        print("PASS: independent watchdog survives killed launcher and reaps stopped child")
    finally:
        sibling.kill()
        sibling.wait()

    runtime = base / "refuse-fullscreen"
    runtime.mkdir()
    runtime.joinpath("engine.ini").write_text((ROOT / "config/engine.ini").read_text().replace("full_screen = 0", "full_screen = 1"))
    run = subprocess.run(command(runtime, 1, "raise Exception('must not execute')"), capture_output=True, text=True, timeout=5)
    assert run.returncode != 0 and "Only bordered windowed runs are allowed" in run.stderr
    assert not list(runtime.glob("runs/*/watchdog.json"))
    print("PASS: fullscreen configuration rejected before process launch")

    runtime = base / "play-heartbeat"
    live = "import os,time; from pathlib import Path; assert 'STORM_INPUT_REPLAY' not in os.environ; assert os.environ['STORM_RELATIVE_MOUSE']=='1'; beat=Path(os.environ['STORM_HEARTBEAT']); end=time.monotonic()+2;\nwhile time.monotonic()<end:\n beat.write_text('alive'); time.sleep(0.1)"
    run = subprocess.run([sys.executable, str(RUNNER), "--play", "--runtime", str(runtime),
                          "--heartbeat-timeout", "0.8", "--", sys.executable, "-c", live],
                         env=os.environ | {"STORM_INPUT_REPLAY": "must-be-removed"},
                         capture_output=True, text=True, timeout=10)
    result = json.loads(await_file(runtime, "result.json").read_text())
    assert run.returncode == 0 and result["timeout"] is None and not result["timed_out"], run.stdout + run.stderr
    print("PASS: play has no fixed deadline, live heartbeat keeps it alive, replay removed")

    runtime = base / "play-hung"
    run = subprocess.run([sys.executable, str(RUNNER), "--play", "--runtime", str(runtime),
                          "--heartbeat-timeout", "0.5", "--", sys.executable, "-c", STOP],
                         capture_output=True, text=True, timeout=10)
    result = json.loads(await_file(runtime, "result.json").read_text())
    assert run.returncode == 124 and result["timeout"] is None and gone(result["pid"])
    assert result["stop_reason"] == "engine heartbeat stopped"
    print("PASS: play watchdog kills unresponsive process without a fixed playtime limit")

print("All safe-run E2E scenarios passed")
