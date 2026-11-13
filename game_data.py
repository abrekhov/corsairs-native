import base64
import json
from pathlib import Path, PurePosixPath
import struct
import zlib

ROOT = Path(__file__).resolve().parent
SETTINGS = ROOT / ".local/settings.json"


def settings():
    if not SETTINGS.exists():
        raise SystemExit("Run: uv run corsairs.py prepare --game /path/to/game")
    return json.loads(SETTINGS.read_text())


def child(directory, name):
    for path in directory.iterdir():
        if path.name.casefold() == name.casefold():
            return path
    raise ValueError(f"Missing {name} in selected game directory")


def decode(data):
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16")
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return data.decode("cp1251")


def repair_parentheses(text):
    output, state, depth, removed, i = [], "", 0, 0, 0
    while i < len(text):
        char, pair = text[i], text[i:i + 2]
        if state in ("\"", "'"):
            output.append(char)
            if char == "\\" and i + 1 < len(text):
                i += 1
                output.append(text[i])
            elif char == state:
                state = ""
        elif state == "line":
            output.append(char)
            if char == "\n":
                state = ""
        elif state == "block":
            output.append(char)
            if pair == "*/":
                output.append("/")
                i += 1
                state = ""
        elif pair in ("//", "/*"):
            output.extend(pair)
            state = "line" if pair == "//" else "block"
            i += 1
        elif char in ("\"", "'"):
            output.append(char)
            state = char
        elif char == ")" and depth == 0:
            removed += 1
        else:
            output.append(char)
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
        i += 1
    return "".join(output), removed


def unpack_program(archive):
    if archive.stat().st_size > 128 * 1024 * 1024:
        raise ValueError("Packed module is too large")
    data = archive.read_bytes()
    if data[-8:] != b"120DT\0\0\0":
        raise ValueError("Unsupported packed PROGRAM format; supply an extracted PROGRAM directory")

    def inflate(offset, limit):
        if not 0 <= offset < len(data) - 12:
            raise ValueError("Invalid packed script offset")
        reader = zlib.decompressobj()
        result = reader.decompress(data[offset:], limit + 1)
        if len(result) > limit or not reader.eof:
            raise ValueError("Invalid or oversized compressed script data")
        return result

    table = inflate(struct.unpack_from("<I", data, len(data) - 12)[0] >> 2, 16 * 1024 * 1024)
    count = struct.unpack_from("<I", table)[0]
    if not 1 <= count <= 10000:
        raise ValueError("Invalid script count")
    position, total, seen = 4, 0, set()
    for _ in range(count):
        length = struct.unpack_from("<I", table, position)[0]
        position += 4
        if not 1 <= length <= 4096 or position + length + 4 > len(table):
            raise ValueError("Invalid script index entry")
        name = base64.b64decode(table[position:position + length], validate=True).decode("cp1251").replace("\\", "/")
        position += length
        offset = struct.unpack_from("<I", table, position)[0] >> 1
        position += 4
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or ":" in name or "\0" in name or name.casefold() in seen:
            raise ValueError("Unsafe or duplicate script path")
        seen.add(name.casefold())
        raw = inflate(offset, 8 * 1024 * 1024)
        if len(raw) < 4 or struct.unpack_from("<I", raw)[0] != len(raw) - 4:
            raise ValueError("Invalid script length")
        total += len(raw)
        if total > 128 * 1024 * 1024:
            raise ValueError("Packed PROGRAM exceeds size limit")
        yield Path(*path.parts), raw[4:]
    if position != len(table):
        raise ValueError("Unexpected trailing script index data")


def prepare_text(game, runtime):
    resource = child(game, "resource")
    ini = child(resource, "ini")
    for name in ("models", "textures", "sounds"):
        child(resource, name)
    report = {"files": 0, "syntax_repairs": 0, "script_source": "directory"}
    try:
        program = child(game, "program")
        child(program, "seadogs.c")
        scripts = ((path.relative_to(program), path.read_bytes()) for path in program.rglob("*") if path.is_file())
    except ValueError:
        archive = child(game, "stormex2.dll")
        scripts = unpack_program(archive)
        report["script_source"] = "packed"
    for relative, raw in scripts:
        target = runtime / "text/program" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if relative.suffix.lower() in (".c", ".h", ".txt", ".ini"):
            text = decode(raw)
            if report["script_source"] == "packed":
                text, removed = repair_parentheses(text)
                report["syntax_repairs"] += removed
            raw = text.encode("utf-8")
        target.write_bytes(raw)
        report["files"] += 1
    child(runtime / "text/program", "seadogs.c")
    for original in ini.rglob("*"):
        if original.is_file():
            target = runtime / "text/ini" / original.relative_to(ini)
            target.parent.mkdir(parents=True, exist_ok=True)
            raw = original.read_bytes()
            if original.suffix.lower() in (".c", ".h", ".ini", ".txt"):
                raw = decode(raw).encode("utf-8")
            target.write_bytes(raw)
            report["files"] += 1
    (runtime / "preparation.json").write_text(json.dumps(report, indent=2) + "\n")
    return report
