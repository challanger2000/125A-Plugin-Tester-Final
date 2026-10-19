#!/usr/bin/env python3
"""Fail-closed 125A source-baseline gate. No network or CI minutes required to inspect locally."""
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

def fail(reason):
    print("BASELINE GATE: FAIL - " + reason, file=sys.stderr)
    sys.exit(1)

def git(*args):
    result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        fail("git " + " ".join(args) + ": " + result.stderr.strip())
    return result.stdout.strip()

def main():
    path = ROOT / "release-baseline.json"
    if not path.is_file():
        fail("release-baseline.json missing; identify authoritative owner-approved source first")
    try:
        m = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        fail("invalid baseline manifest: " + str(exc))
    if m.get("verified") is not True:
        fail("latest approved binary/source provenance is NOT VERIFIED; no release build permitted")
    version = m.get("approved_version")
    sha = m.get("approved_source_commit")
    if not isinstance(version, str) or not re.fullmatch(r"0\.2\.\d+", version):
        fail("approved_version missing/invalid")
    if not isinstance(sha, str) or not re.fullmatch(r"[0-9a-f]{40}", sha):
        fail("approved_source_commit missing/invalid")
    actual = re.search(r"project\(PluginTester125A VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not actual or actual.group(1) != version:
        fail("CMake version differs from verified approved version " + version)
    result = subprocess.run(["git", "merge-base", "--is-ancestor", sha, "HEAD"], cwd=ROOT)
    if result.returncode:
        fail("approved source commit is not an ancestor of checked-out HEAD")
    if m.get("approved_package_sha256") is None or not re.fullmatch(r"[0-9a-f]{64}", str(m["approved_package_sha256"])):
        fail("approved package SHA-256 is missing")
    print("BASELINE GATE: PASS - version", version, "source", sha, "head", git("rev-parse", "--short", "HEAD"))

if __name__ == "__main__":
    main()
