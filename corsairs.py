import argparse
import json
from pathlib import Path
import platform
import subprocess
import sys

from game_data import ROOT, SETTINGS, prepare_text, settings

parser = argparse.ArgumentParser(description="Native Apple Silicon launcher for City of Abandoned Ships")
commands = parser.add_subparsers(dest="command", required=True)
build = commands.add_parser("build")
build.add_argument("--jobs", type=int, default=2)
prepare = commands.add_parser("prepare")
prepare.add_argument("--game", type=Path, required=True)
prepare.add_argument("--runtime", type=Path, default=Path.home() / "Library/Application Support/CorsairsNative")
play = commands.add_parser("play")
play.add_argument("--window", choices=("space", "fit"), default="space")
test = commands.add_parser("test")
test.add_argument("--timeout", type=int, default=30)
args = parser.parse_args()

if platform.system() != "Darwin" or platform.machine() != "arm64":
    raise SystemExit("Apple Silicon macOS is required; run this shell natively, not through Rosetta")
if args.command == "build":
    if not 1 <= args.jobs <= 16:
        parser.error("--jobs must be between 1 and 16")
    subprocess.run(["cmake", "-S", str(ROOT), "-B", str(ROOT / "build"), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"], check=True)
    subprocess.run(["cmake", "--build", str(ROOT / "build"), "--target", "engine", "--parallel", str(args.jobs)], check=True)
elif args.command == "prepare":
    game, runtime = args.game.expanduser().resolve(), args.runtime.expanduser().resolve()
    if not game.is_dir() or game == runtime or runtime.is_relative_to(game) or game.is_relative_to(runtime):
        parser.error("Choose a game directory and a separate runtime directory")
    marker = runtime / ".corsairs-native"
    if runtime.exists() and any(runtime.iterdir()) and not marker.is_file():
        parser.error("Refusing to modify a nonempty directory not owned by this launcher")
    runtime.mkdir(parents=True, exist_ok=True)
    marker.touch()
    report = prepare_text(game, runtime)
    SETTINGS.parent.mkdir(exist_ok=True)
    SETTINGS.write_text(json.dumps({"game_dir": str(game), "runtime": str(runtime)}, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    print("Prepared separate runtime; the source game and existing saves were not overwritten")
else:
    runtime = Path(settings()["runtime"])
    command = [sys.executable, str(ROOT / "run-native.py"), "--runtime", str(runtime)]
    if args.command == "play":
        command += ["--play", "--window", args.window]
    else:
        command += ["--timeout", str(args.timeout)]
    raise SystemExit(subprocess.run(command).returncode)
