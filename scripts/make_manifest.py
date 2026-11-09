#!/usr/bin/env python3
"""Write manifest.json for an Eridian Translator release.

Usage: make_manifest.py <repo owner/name> <version> <tag> <dist_dir> [notes]

Expects <dist_dir>/eridian-<variant>.bin for each variant. The manifest points
at the stable github.com release URLs (never the signed redirect targets),
which is what the firmware's URL check requires.
"""
import hashlib
import json
import sys
from pathlib import Path

VARIANTS = ["amoled18"]


def main() -> None:
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    repo, version, tag, dist = sys.argv[1], sys.argv[2], sys.argv[3], Path(sys.argv[4])
    notes = sys.argv[5] if len(sys.argv) > 5 else ""
    builds = {}
    for variant in VARIANTS:
        path = dist / f"eridian-{variant}.bin"
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        builds[variant] = {
            "url": f"https://github.com/{repo}/releases/download/{tag}/{path.name}",
            "sha256": digest,
        }
        print(f"{variant}: {path.stat().st_size} bytes, sha256 {digest}")
    manifest = {"version": version, "notes": notes, "builds": builds}
    (dist / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print((dist / "manifest.json").read_text())


if __name__ == "__main__":
    main()
