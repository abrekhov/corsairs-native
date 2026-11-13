import argparse
import configparser
import fcntl
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from game_data import child, settings

ROOT = Path(__file__).resolve().parent
ENGINE = ROOT / "build/bin/engine"


def prepare(runtime):
    runtime.mkdir(parents=True, exist_ok=True)
    ini = runtime / "engine.ini"
    if not ini.exists():
        ini.write_bytes((ROOT / "config/engine.ini").read_bytes())
    config = configparser.ConfigParser(interpolation=None, strict=True)
    config.read_string("[engine]\n" + ini.read_text())
    values = config["engine"]
    if values.getint("full_screen") != 0 or values.getint("window_borders") != 1:
        raise ValueError("Only bordered windowed runs are allowed")
    if not (320 <= values.getint("screen_x") <= 4096 and 240 <= values.getint("screen_y") <= 2160):
        raise ValueError("Window size is outside the sane range")
    config = settings()
    game = Path(config["game_dir"])
    prepared = Path(config["runtime"])
    program = runtime / "text/program"
    links = {runtime / "PROGRAM": program if program.is_dir() else prepared / "text/program"}
    for source in child(game, "resource").iterdir():
        if source.name.casefold() in ("techniques", "shared"):
            continue
        ini_text = runtime / "text/ini"
        if not ini_text.is_dir():
            ini_text = prepared / "text/ini"
        target = ini_text if source.name.lower() == "ini" else source
        links[runtime / "resource" / source.name] = target
    for name in ("techniques", "shared"):
        links[runtime / "resource" / name] = ROOT / "build/bin/resource" / name
    for link, target in links.items():
        link.parent.mkdir(parents=True, exist_ok=True)
        if link.is_symlink() and link.resolve() == target.resolve():
            continue
        if link.exists() or link.is_symlink():
            raise ValueError(f"Refusing to replace {link}")
        if not target.exists():
            raise FileNotFoundError(target)
        link.symlink_to(target, target_is_directory=target.is_dir())


def save(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def supervise(args):
    run_dir = args.run_dir
    started = time.monotonic()
    result = {"watchdog_pid": os.getpid(), "command": args.command, "timeout": args.timeout,
              "mode": "play" if args.play else "test", "heartbeat_timeout": args.heartbeat_timeout}
    heartbeat = run_dir / "heartbeat"
    process = None
    try:
        with (args.runtime / ".run.lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
            env = os.environ | {
                "STORM_SAFE_WINDOW": "0" if args.play else "1",
                "STORM_RELATIVE_MOUSE": "1" if args.play or args.capture_mouse else "0",
                "STORM_ALLOW_DESKTOP_WINDOW": "1" if args.play else "0",
                "STORM_WINDOW_SIZE": (args.window or ("fit" if args.play else "")),
                "STORM_HEARTBEAT": str(heartbeat),
                "STORM_USER_PATH": str(args.runtime / "user"),
                "SDL_MOUSE_AUTO_CAPTURE": "0",
                "SDL_GRAB_KEYBOARD": "0",
                "SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS": "0",
            }
            prefix = subprocess.check_output(["brew", "--prefix", "molten-vk"], text=True).strip()
            icd = Path(prefix) / "etc/vulkan/icd.d/MoltenVK_icd.json"
            if not icd.is_file():
                raise FileNotFoundError("MoltenVK ICD missing; run brew bundle")
            env["VK_ICD_FILENAMES"] = str(icd)
            if args.play:
                env.pop("STORM_INPUT_REPLAY", None)
            if not env["STORM_WINDOW_SIZE"]:
                env.pop("STORM_WINDOW_SIZE", None)
            with (run_dir / "engine.log").open("wb") as log:
                process = subprocess.Popen(args.command, cwd=args.runtime, env=env, stdout=log,
                                           stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                           start_new_session=True)
                result["pid"] = process.pid
                try:
                    save(run_dir / "watchdog.json", result)
                    print(f"Watchdog {os.getpid()} armed for PID {process.pid}: timeout={args.timeout}, heartbeat={args.heartbeat_timeout}s", flush=True)
                    result["timed_out"] = False
                    last_heartbeat = time.monotonic()
                    heartbeat_stamp = None
                    while process.poll() is None:
                        try:
                            process.wait(timeout=0.1)
                        except subprocess.TimeoutExpired:
                            pass
                        now = time.monotonic()
                        if heartbeat.exists():
                            stamp = heartbeat.stat().st_mtime_ns
                            if stamp != heartbeat_stamp:
                                heartbeat_stamp = stamp
                                last_heartbeat = now
                        if args.timeout is not None and now - started >= args.timeout:
                            result["timed_out"] = True
                            result["stop_reason"] = "test deadline"
                            break
                        if now - last_heartbeat >= args.heartbeat_timeout:
                            result["timed_out"] = True
                            result["stop_reason"] = "engine heartbeat stopped"
                            break
                finally:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    result["returncode"] = process.wait()
    except Exception as error:
        result["error"] = str(error)
    result["elapsed_seconds"] = round(time.monotonic() - started, 3)
    save(run_dir / "result.json", result)
    print(json.dumps(result), flush=True)
    return 1 if "error" in result else 124 if result["timed_out"] else result["returncode"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=float)
    parser.add_argument("--play", action="store_true")
    parser.add_argument("--capture-mouse", action="store_true")
    parser.add_argument("--window", default="", help="'' inherits config; 'fit' for display-sized desktop window; or WxH")
    parser.add_argument("--heartbeat-timeout", type=float, default=90)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--watchdog", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--run-dir", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.timeout is None and not args.play:
        args.timeout = 30
    if args.timeout is not None and not 0 < args.timeout <= 300:
        parser.error("Timeout must be greater than zero and at most 300 seconds")
    if not 0 < args.heartbeat_timeout <= 300:
        parser.error("Heartbeat timeout must be greater than zero and at most 300 seconds")
    args.runtime = (args.runtime or Path(settings()["runtime"])).resolve()
    args.command = args.command[1:] if args.command[:1] == ["--"] else args.command
    args.command = args.command or [str(ENGINE)]
    if args.watchdog:
        return supervise(args)
    prepare(args.runtime)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    run_dir = args.runtime / "runs" / stamp
    run_dir.mkdir(parents=True)
    with (run_dir / "watchdog.log").open("wb") as log:
        options = ["--heartbeat-timeout", str(args.heartbeat_timeout)]
        if args.timeout is not None:
            options += ["--timeout", str(args.timeout)]
        if args.play:
            options += ["--play"]
        if args.capture_mouse:
            options += ["--capture-mouse"]
        if args.window:
            options += ["--window", args.window]
        watchdog = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), "--watchdog",
                                     "--runtime", str(args.runtime), "--run-dir", str(run_dir),
                                     *options, "--", *args.command],
                                    stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                    start_new_session=True)
    print(f"Run: {run_dir}\nIndependent watchdog: {watchdog.pid}", flush=True)
    try:
        watchdog.wait()
    except KeyboardInterrupt:
        print("Watchdog remains armed; the test process will be stopped at its deadline.", flush=True)
        return 130
    result_file = run_dir / "result.json"
    if not result_file.exists():
        raise RuntimeError(f"Watchdog failed; inspect {run_dir / 'watchdog.log'}")
    result = json.loads(result_file.read_text())
    print(json.dumps(result, indent=2))
    return 1 if "error" in result else 124 if result["timed_out"] else result["returncode"]


if __name__ == "__main__":
    sys.exit(main())
