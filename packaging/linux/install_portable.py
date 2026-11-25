# SPDX-License-Identifier: AGPL-3.0-or-later
"""Install or remove Risco in a user-owned prefix; never uses sudo."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tempfile

APP = "risco"
PAYLOAD = "lib/risco"
RECEIPT = "share/risco-install/receipt.json"
LIMIT_FILES = 128
LIMIT_BYTES = 128 * 1024**2


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_path(name):
    path = PurePosixPath(name)
    if not name or path.is_absolute() or any(x in ("", ".", "..") for x in name.split("/")):
        raise ValueError("unsafe package path: " + name)
    if any(c in name for c in "\n\r\t\\\0"):
        raise ValueError("unsupported package path")
    return path


def reject_symlinks(path):
    for part in (path, *path.parents):
        if part.is_symlink():
            raise ValueError("symlink path refused: " + str(part))


def atomic_write(path, data, mode):
    reject_symlinks(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".risco-install-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
        os.chmod(temporary, mode)
        reject_symlinks(path)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def load_receipt(prefix):
    path = prefix / RECEIPT
    reject_symlinks(path)
    if not path.exists():
        return {}
    record = json.loads(path.read_text(encoding="utf-8"))
    if record.get("schema") != 1 or record.get("prefix") != str(prefix):
        raise ValueError("unknown Risco installation receipt")
    files = record.get("files")
    if not isinstance(files, dict) or len(files) > LIMIT_FILES:
        raise ValueError("invalid Risco installation receipt")
    for name, sha in files.items():
        safe_path(name)
        if not isinstance(sha, str) or len(sha) != 64:
            raise ValueError("invalid checksum in installation receipt")
    return files


def package_files(package):
    sums = package / "SHA256SUMS"
    reject_symlinks(sums)
    selected, total = {}, 0
    lines = sums.read_text(encoding="utf-8").splitlines()
    if len(lines) > LIMIT_FILES:
        raise ValueError("too many files in Risco package")
    for line in lines:
        sha, separator, name = line.partition("  ")
        safe_path(name)
        if not separator or len(sha) != 64 or name in selected:
            raise ValueError("invalid Risco package checksum list")
        source = package / name
        reject_symlinks(source)
        if not source.is_file():
            raise ValueError("missing package file: " + name)
        data = source.read_bytes()
        total += len(data)
        if total > LIMIT_BYTES or digest(data) != sha:
            raise ValueError("Risco package checksum/budget failure: " + name)
        selected[name] = data
    if "usr/bin/Risco" not in selected or "usr/share/applications/risco.desktop" not in selected:
        raise ValueError("incomplete Risco application payload")
    return selected


def target_files(package, prefix):
    selected = package_files(package)
    targets = {}
    for name, data in selected.items():
        if not name.startswith("usr/"):
            raise ValueError("unexpected file in Risco payload: " + name)
        relative = PAYLOAD + "/usr/" + name[4:]
        mode = 0o755 if name == "usr/bin/Risco" else 0o644
        targets[relative] = (data, mode)

    executable = prefix / PAYLOAD / "usr/bin/Risco"
    launcher = '#!/bin/sh\nexec "' + str(executable) + '" "$@"\n'
    targets["bin/Risco"] = (launcher.encode("utf-8"), 0o755)

    desktop_name = "usr/share/applications/risco.desktop"
    desktop = selected[desktop_name].decode("utf-8").splitlines()
    if sum(line.startswith("Exec=") for line in desktop) != 1:
        raise ValueError("invalid Risco desktop entry")
    icon = prefix / PAYLOAD / "usr/share/icons/hicolor/512x512/apps/risco.png"
    if sum(line.startswith("Icon=") for line in desktop) != 1:
        raise ValueError("invalid Risco icon entry")
    desktop = ["Exec=" + str(prefix / "bin/Risco") if line.startswith("Exec=")
               else "Icon=" + str(icon) if line.startswith("Icon=") else line
               for line in desktop]
    targets["share/applications/risco.desktop"] = (
        ("\n".join(desktop) + "\n").encode("utf-8"), 0o644)
    return targets


def save_receipt(prefix, files):
    data = json.dumps({"schema": 1, "prefix": str(prefix), "files": files},
                      ensure_ascii=False, indent=2) + "\n"
    atomic_write(prefix / RECEIPT, data.encode("utf-8"), 0o600)


def install(package, prefix):
    targets = target_files(package, prefix)
    old = load_receipt(prefix)
    for name in set(old) | set(targets):
        path = prefix / name
        reject_symlinks(path)
        if path.exists():
            current = digest(path.read_bytes())
            if name not in old or current != old[name]:
                raise ValueError("unknown or modified file preserved: " + str(path))
    installed = dict(old)
    for name, (data, mode) in targets.items():
        atomic_write(prefix / name, data, mode)
        installed[name] = digest(data)
        save_receipt(prefix, installed)
    print("Installed Risco in", prefix)


def remove(prefix):
    files = load_receipt(prefix)
    for name in files:
        reject_symlinks(prefix / name)
    kept = {}
    for name, sha in files.items():
        path = prefix / name
        if path.exists():
            if digest(path.read_bytes()) == sha:
                path.unlink()
            else:
                kept[name] = sha
                print("Modified file preserved:", path)
    receipt = prefix / RECEIPT
    if kept:
        save_receipt(prefix, kept)
    elif receipt.exists():
        receipt.unlink()
    for base in (prefix / PAYLOAD, prefix / "share/applications", prefix / "bin",
                 prefix / "share/risco-install", prefix / "share"):
        try:
            base.rmdir()
        except OSError:
            pass
    print("Removed registered Risco files; user compositions and settings were left alone.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--remove", action="store_true")
    parser.add_argument("--prefix", default=os.environ.get("PREFIX", str(Path.home() / ".local")))
    args = parser.parse_args()
    try:
        prefix = Path(os.path.abspath(args.prefix))
        if prefix == Path("/") or any(c in str(prefix) for c in '\n\r\t"%`$\\'):
            raise ValueError("unsupported installation prefix")
        reject_symlinks(prefix)
        if args.remove:
            remove(prefix)
        else:
            install(Path(__file__).resolve().parent, prefix)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
