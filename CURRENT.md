# CURRENT — 125A Plugin Tester version navigation

**Verified on:** 2026-10-26. **Product:** internal Windows x64 VST3 QA utility, **not** a commercial Gumroad FULL plug-in.

## Three distinct states — do not conflate

| Classification | Version | Evidence | What it does NOT establish |
|---|---|---|---|
| **Published GitHub FINAL release** | **v0.2.10** | [Immutable release v0.2.10](https://github.com/challanger2000/125A-Plugin-Tester-Final/releases/tag/v0.2.10); source/tag `ec2db65b9b9dbce2b7a5d146cb54869c9ad82e6d`; [Windows Actions #38744097119 SUCCESS](https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/38744097119) | Does not mean `main` includes it or that later candidates are published |
| **Most recent confirmed Windows QA development candidate** | **v0.3.2** | Branch `fix/v0.3.2-mixfx-realtime-evidence`; [source commit `91c975536a45190085304dcc28a43d93ff56ca88`](https://github.com/challanger2000/125A-Plugin-Tester-Final/commit/91c975536a45190085304dcc28a43d93ff56ca88); [Windows Actions #39823684764 SUCCESS (2026-10-22)](https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/39823684764); artifact `125A-Plugin-Tester-v0.3.2-Windows-x64` (artifact ID `12445333317`) | **Not** a GitHub v0.3.2 release, merge to `main`, owner acceptance of a packaged ZIP, or proof of real Studio One use |
| **Default `main` code snapshot** | **v0.2.9** | Root `CMakeLists.txt` on `main` says 0.2.9 | Not a valid shortcut to determine the most recent published/tested version |

The official v0.2.10 release provides `125A_Plugin_Tester.exe` (SHA-256 `b0f386422d107ac6fa8b15c3e1e86113f52b013e277fc94b16d28b929438bfea`) and `125A_Plugin_Tester_v0.2.10_Windows_x64_FINAL.zip` (SHA-256 `e5b152fa5b05140f5dfc883c7dd736f572ca50c13b78d90be7505ec6e13e83af`). These hashes describe **v0.2.10 only** and must not be reused for v0.3.2.

**Owner-reported local package:** `125A_Plugin_Tester_V0.3.2_Windows_mit_Handbuch.zip`. The package filename is known from the owner, but its bytes, contents, manual, SHA-256 and correspondence with Actions #39823684764 are **NOT VERIFIED** in this repository. Confirm them with the owner-supplied file/checksum before recording the ZIP as an approved distributable.

**SDK:** both checked source versions pin Steinberg VST3 SDK `v3.8.1_build_84`. Do not infer the tester application version from the SDK tag.

## Mandatory selection for future QA and Codex work

1. Read `125A-Engineering/START-HERE.md` and this repository's root `AGENTS.md`.
2. Inspect this file, the real [GitHub Releases](https://github.com/challanger2000/125A-Plugin-Tester-Final/releases), relevant [branches](https://github.com/challanger2000/125A-Plugin-Tester-Final/branches), their HEAD commits, and successful Windows workflows/artifacts **before choosing a version**. Recheck because this dated record can become stale.
3. For the **last published official release**, use the exact pinned **v0.2.10** package/source. For **latest development QA coverage**, evaluate the pinned **v0.3.2** Windows artifact (and any subsequent newer, independently verified candidate) and document its development status. Do not silently downgrade to v0.2.9 because `main` has older code.
4. Document source SHA, built artifact identity, Windows QA evidence and exact version used in each consuming project's test report. Do not equate a green Windows build with a full manual DAW acceptance.
5. Keep the official release baseline/manifest for v0.2.10 unchanged until a later version is independently release-approved; updating this *navigation document* **does not authorize a new release**.

**Documentation consistency rule:** On the next verified development build, update `CURRENT.md`, `README.md` and `AGENTS.md` together. On approval of a new official tester release, also reconcile the release-baseline manifest, tag, source commit and package hashes, and verify what is displayed from `main` after merge. Never leave the owner to reconcile stale navigation between chats.
