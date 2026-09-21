# 125A Plugin Tester / Quality Checker

**Version 0.2.7 – Windows x64**

125A Plugin Tester is a portable VST3 quality-assurance tool for Windows. The public package contains exactly one executable, `125A_Plugin_Tester.exe`, which embeds and extracts the internal helper tools required for isolated testing.

## What it checks

- VST3 module loading and factory/class discovery
- Component/controller creation, initialization, connection and teardown
- Audio/event bus metadata, arrangements and activation
- Common sample rates and buffer sizes
- 32-bit processing stability and finite-output checks
- Sustained processing and processing-state lifecycle
- Offline processing
- Component/controller state roundtrips
- Fresh-instance state verification
- Reload/instantiation stress
- Native editor lifecycle: create, attach, runtime/message pump, detach and destroy
- Isolated I/O, sidechain and event probing
- Official Steinberg VST3 validator
- Crash, hang and timeout isolation using Windows Job Objects
- Single plug-in tests and recursive serial folder scans
- Human-readable TXT reports and structured JSON reports

## Result model

- **PASS** – all applicable checks passed
- **WARNING** – no deterministic failure, but review is required
- **FAIL** – a reproducible plug-in contract, lifecycle, state, processing or validator failure was detected
- **INCONCLUSIVE** – the test could not be completed reliably because of a crash, timeout or tester/OS infrastructure problem

## Internal acceptance fixtures

The development workflow verifies both specificity and sensitivity against deliberate VST3 fixtures:

- Good Control
- Bad Lifecycle
- Bad NaN
- Bad State
- Bad I/O
- Good Editor
- Bad Editor

The Bad I/O fixture must preserve the exact diagnosis:

`Bus arrangement - out-of-range bus index was accepted`

The workflow also validates folder-scan aggregation, the production Job Object guard, the Steinberg validator wrapper and the final single-file portable executable.

## v0.2.7 maintenance pass

- fixed the remaining GUI header macro redefinition warning
- enabled **/WX** for all 125A-owned Windows targets so future compiler warnings fail the build
- synchronized all tester/report version strings to 0.2.7
- retained direct single-VST3 and recursive folder-test workflows
- retained the one-public-EXE packaging model
- removed stale pre-0.2.7 binary artifacts from the source branch

## Usage

Run `125A_Plugin_Tester.exe`, select a VST3 plug-in or a VST3 folder, and start the test. Reports are written to the 125A Plugin Tester report directory under the current Windows user profile.

## Platform

- Windows x64
- VST3
- Standalone portable executable
- Steinberg VST3 SDK 3.8.1 baseline

## License note

This project uses the Steinberg VST3 SDK under its applicable licensing terms. 125A project material is all rights reserved unless explicitly stated otherwise.
