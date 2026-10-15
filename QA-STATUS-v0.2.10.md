# 125A Plugin Tester v0.2.10 — FINAL Release QA

**Officially published GitHub FINAL:** https://github.com/challanger2000/125A-Plugin-Tester-Final/releases/tag/v0.2.10

- Source and tag: `ec2db65b9b9dbce2b7a5d146cb54869c9ad82e6d`.
- Full Windows QA PASS: https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/38744097119
- Portable Windows EXE SHA-256: `b0f386422d107ac6fa8b15c3e1e86113f52b013e277fc94b16d28b929438bfea`.
- GitHub FINAL ZIP SHA-256: `e5b152fa5b05140f5dfc883c7dd736f572ca50c13b78d90be7505ec6e13e83af`.
- 16 VST3 fixture kinds (0–15) passed expected positive/negative classification, including imported heap and potentially blocking wait callback detections.
- Imported-Win32-API measurement is **scoped**: custom allocators, statically linked/indirect calls and other lock mechanisms are not universally detectable. A missing signal is not proof of a fully allocation/lock-free plugin.
- `main` retains former stable v0.2.9.

---

# Historical v0.2.10 expanded candidate verification

**Status: WINDOWS QA PASS — tested development candidate, not merged to stable main.**

## Expanded full-matrix QA, October 15, 2026

The newer candidate has a **successful complete Windows Actions QA** for source commit `63b69e4070f28265e01c01125e8e2382d55cf096`.

- Workflow: https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/38741087175
- Artifact ID: `11976083096` (`125A-Plugin-Tester-Windows-x64`).
- ZIP SHA-256: `1bfff33e3397bf515f330d2128e9719e2cdc403feb086c2f50ad7abb94fae265`.
- Exactly one public Windows x64 EXE inside the ZIP (2,194,944 bytes).
- Full independently guarded 100-case matrix: 32/64-bit, 44.1/48/88.2/96/192 kHz and all ten 1–8192 frame blocks. Unsupported formats remain nonfailures when at least one supported profile passes.
- Added a 32-block host-paced processing stress. It separately reports audio-callback deadline overruns and Windows host-scheduler misses; only actual callback budget overruns warn.
- Existing fourteen positive/negative fixtures, standalone packaging, report classification, state, GUI/audio concurrency, MIDI and portable end-to-end tests all passed in this run.

**Technical limit:** heap allocations and lock contention *inside third-party plug-in DLLs* are still not directly traced. Stopwatch measurements alone cannot prove allocation-free / lock-free audio execution. The full request for direct allocation/lock detection therefore remains open and must not be represented as complete.

---

## Reproducible artifact identity

- Exact tested source commit: `deadc71d0aad889bd1a0ad6a9f091ee7dcf3e91d`.
- Successful workflow: https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/38737323886
- Workflow artifact: `125A-Plugin-Tester-Windows-x64`, artifact ID `11974353136`.
- Downloaded artifact ZIP SHA-256: `7524fab14fa3aa9e6ba2bd9ae9570083fff3c0447ba246859202a4ede14096d5`.
- ZIP verified: one public `125A_Plugin_Tester.exe` (2,188,800 bytes); ZIP CRC check passed.
- Steinberg VST3 SDK pinned to `v3.8.1_build_84`.
- Stable `main` remains the v0.2.9 reference and is not modified by this branch.

## Verified CI gates

- Windows x64 CMake Configure, full Release Build.
- Dispatcher classification and report-write selftests, including contradictory exits, stale/incomplete reports, guarded outcome precedence and intentional JSON write failures.
- Production Windows Job Object guard selftest (success, failure, crash, hang).
- Steinberg validator selftest and full QA fixture validation.
- Fourteen positive and negative VST3 fixtures (types 0–13): standard good, bad lifecycle, invalid NaN, bad state, bad I/O, good editor, bad editor, ignored state, 64-bit-only NaN, correct gain/bypass, ignored gain, concurrent editor/audio defect, correct event echo and dropped event.
- Deterministic functional gain/bypass state-recall outputs compared against expected samples.
- 32/64-bit actual audio matrix, sample-rate and block-size testing; supported profile handling without false warnings for unsupported formats.
- 16 separately launched **guarded Windows processes per examined plugin** to cover 32/64-bit × 44.1/48/96/192 kHz × 32/1024 sample maximum block sizes, with 3 consecutive processing calls and changing actual frame counts per run.
- Broader same-instance actual processing matrix across 4 sample rates × 5 block sizes per supported precision.
- Six sequential event/transport blocks; play/stop, tempo, time-signature, position jumps, and a deterministic 16-event echo contract with dropped-event negative control.
- Repeated editor open/attach/detach/reopen, real simultaneous GUI/audio processing for supported bus layouts, minimize/restore, supported resize and optional content scaling at 100/150%.
- Per-block execution p95, p99, max and deadline-overrun diagnostics.
- Serial folder scan with aggregate JSON; portable single-file EXE end-to-end test; only one public EXE included.
- Reporter version, exact source revision and pinned SDK metadata embedded in worker reports.

## All nine source-audit defect groups addressed in development

1. Inactive audio-bus pointer arrays.
2. Report/exit-code contradictory result classification.
3. Recursive discovery permission errors.
4. Inconsistent version strings and metadata.
5. Host controller `IComponentHandler`.
6. Misleading GUI status on incomplete outcomes.
7. Infrastructure write errors for QA, JSON, summary and progress reports.
8. Latency and tail query lifecycle.
9. Controller creation failure handling in editor and reload probes.

## Interpretation and remaining limits

**All eight requested *areas* now have implemented checks**, but not every proposed subcase is universally detectable without plugin-specific contracts or runtime instrumentation:

- Universal semantic State and expected audio assertions cannot be inferred for arbitrary third-party plug-ins; reference fixtures supply explicit sample-level contracts.
- Generic detection of internal audio-thread heap allocations and contended locks is **not implemented**; per-block latency/overrun measurements detect symptoms, not root-cause allocations/locks.
- True automation/state switching *simultaneous* with GUI plus active audio is not fully exercised for every arbitrary plugin. GUI/audio overlap is exercised where a single mono/stereo input/output configuration is supported.
- The isolated matrix covers 16 representative combinations; the wider matrix executes additional configurations inside a single worker process.
- Deterministic audio deadline measurements are informative, not a complete paced realtime scheduling benchmark under every host/load configuration.
- The tester is a Windows EXE, not a plugin that is opened inside Studio One.

Passing CI proves these executed checks, not universal VST3 correctness. No commercial FINAL publication or merge to `main` has been performed.
