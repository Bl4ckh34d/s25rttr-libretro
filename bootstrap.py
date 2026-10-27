"""Fetch the pinned upstream source and apply the libretro integration patches."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent

def git(path, *args, check=True):
    return subprocess.run(["git", "-C", str(path), *args], check=check,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "deps/s25rttr-android")
    parser.add_argument("--check", action="store_true", help="Verify an already patched tree without changing it")
    args = parser.parse_args()
    source = args.source.resolve()
    manifest = json.loads((ROOT / "upstream.json").read_text())
    if not source.exists():
        if args.check:
            raise SystemExit("Source directory does not exist")
        source.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", "--no-checkout", manifest["repository"], str(source)], check=True)
        git(source, "checkout", "--detach", manifest["revision"])
    if git(source, "rev-parse", "HEAD").stdout.strip() != manifest["revision"]:
        raise SystemExit("Unexpected upstream revision; use a separate checkout")
    if not args.check:
        git(source, "submodule", "update", "--init", "--recursive")
    for entry in manifest["patches"]:
        component = source / entry["path"]
        if git(component, "rev-parse", "HEAD").stdout.strip() != entry["revision"]:
            raise SystemExit(f"Unexpected revision: {entry['path']}")
        patch = str(ROOT / entry["patch"])
        if git(component, "apply", "--reverse", "--check", patch, check=False).returncode == 0:
            print(f"Verified applied patch: {entry['path']}")
            continue
        if args.check:
            raise SystemExit(f"Patch missing or source differs: {entry['path']}")
        result = git(component, "apply", "--check", patch, check=False)
        if result.returncode:
            raise SystemExit(result.stderr)
        git(component, "apply", patch)
        print(f"Applied patch: {entry['path']}")
    print(f"Ready: {source}")

if __name__ == "__main__":
    main()
