#!/usr/bin/env python3
"""Reject CI builds not descending from the independently verified release baseline."""
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

def fail(s):
    print("BASELINE GATE FAIL: " + s, file=sys.stderr)
    sys.exit(1)

def main():
    try:
        m = json.loads((ROOT / "release-baseline.json").read_text(encoding="utf-8"))
        cm = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    except (OSError, ValueError) as ex:
        fail(str(ex))
    version = m.get("approved_version")
    source = m.get("approved_source_commit")
    digest = m.get("official_release_zip_sha256")
    if m.get("verified") is not True:
        fail("unverified source provenance")
    if not isinstance(version,str) or not re.fullmatch(r"0\.2\.\d+",version):
        fail("invalid version")
    if not isinstance(source,str) or not re.fullmatch(r"[0-9a-f]{40}",source):
        fail("invalid source SHA")
    if not isinstance(digest,str) or not re.fullmatch(r"[0-9a-f]{64}",digest):
        fail("missing official package digest")
    actual = re.search(r"project\(PluginTester125A VERSION ([0-9.]+)",cm)
    if not actual or actual.group(1)!=version:
        fail("CMake project version does not match approved baseline")
    if subprocess.run(["git","cat-file","-e",source+"^{commit}"],cwd=ROOT).returncode:
        fail("approved release commit missing")
    if subprocess.run(["git","merge-base","--is-ancestor",source,"HEAD"],cwd=ROOT).returncode:
        fail("HEAD does not descend from the verified approved release commit")
    print("BASELINE GATE PASS:",version,source)

if __name__=="__main__":
    main()
