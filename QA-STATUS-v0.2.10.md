# v0.2.10 Windows x64 Candidate — QA record

Status: **Windows CI PASS; candidate, not merged to stable main**.

- Source commit verified by CI: `22560722c9770c0c80380539c4e53896bd5ff43f`.
- Windows QA run: https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/38733381439
- GitHub artifact: `125A-Plugin-Tester-Windows-x64`, artifact ID `11971374065`.
- Artifact ZIP SHA-256 (downloaded verified ZIP): `827d50ebe93acea8d5370de28b9144fd55ca86d11d252e19ffc0be1117192f0c`.
- ZIP contains exactly one `125A_Plugin_Tester.exe` (portable Windows x64), verified by ZIP inspection.
- Stable `main` v0.2.9 remains unchanged.

## Proven CI checks

Windows x64 Configure / Build; dispatcher classifier and report-write self-tests; production Job Object crash/hang tests; Steinberg validator self-test; positive and negative VST3 fixtures (including silent ignored state, 64-bit-only invalid output and ignored gain), structured TXT/JSON report assertions, folder aggregation, single-file portable packaging, portable EXE end-to-end execution.

## Source fixes addressed

1. Inactive audio bus channel pointer arrays.
2. Exit-code/report contradictions and invalid/incomplete result counts.
3. Recursive scan permission error propagation.
4. Version consistency in executable and report metadata.
5. Host `IComponentHandler` registration on active controller in main worker.
6. GUI safeguards against false PASS for failed or incomplete scans/reports.
7. Worker, per-plugin JSON, summary, and progress report write-failure propagation.
8. Latency and tail queried after processing setup.
9. Controller creation failures detected in reload/editor probes.

## Additional test coverage actually introduced

- Parameter string/normalized conversion roundtrips.
- Deterministic semantic reference-state and measured gain/bypass response checks.
- Actual 32/64-bit processing across 4 sample rates, 5 block sizes, and 3 consecutive blocks per profile.
- 64-bit-only NaN fixture, silently ignored-state fixture, functional gain reference and ignored-gain control.
- Sustained per-block p95/p99/max execution times and 48 kHz deadline-overrun counters.
- Dispatcher classification and report-write infrastructure selftests.

## Limitations — not claimed as fully implemented

The matrix currently reuses one component instance and does not isolate **every rate/block profile in a fresh process**. Comprehensive varying-block-on-one-active-instance coverage, MIDI transport/loop jump sequences, GUI activity concurrent with actual audio processing, runtime hook-based allocation/lock detection, output-event overflow fixtures and comprehensive positive/negative fixture variety remain additional development work. General-purpose plug-ins have no known expected audible output and therefore cannot pass a universal gain-response oracle; the deterministic functional contract is limited to reference fixtures. Deadline measures in this build are diagnostic and not a substitute for paced host-like realtime performance checks.

CI PASS is evidence for the tests actually run, not proof of universal plug-in compatibility or completion of every proposed future test enhancement.
