# 125A Plugin Tester / Quality Checker

**Version 0.2.0 – Windows x64**

The 125A Plugin Tester is a standalone Windows quality-assurance tool for VST3 plug-ins. It combines structural validation, lifecycle checks, processing stress tests, state testing, isolated I/O/event probing and the official Steinberg VST3 validator in one portable executable.

## What it checks

- VST3 module loading and factory classes
- Component/controller creation and connection
- Audio/event bus metadata and activation
- Processing setup across common sample rates and buffer sizes
- 32-bit processing stability and finite-output checks
- Sustained processing and lifecycle stress
- Offline processing
- Component/controller state roundtrips
- Fresh-instance state verification
- Reload/instantiation stress
- I/O, sidechain and event isolation
- Official Steinberg VST3 validator
- Human-readable TXT reports and structured JSON reports

## Release status

The distributed executable is the exact artifact produced by successful development workflow **Build #61** from the verified `main` state.

The final BadIO acceptance test confirmed that the tester correctly detects the deliberately invalid bus-arrangement contract violation:

`Bus arrangement - out-of-range bus index was accepted`

The same fixture still passes the official Steinberg validator, demonstrating that the 125A-specific QA layer can expose deterministic problems beyond the validator's standard test set.

## Usage

Run `125A_Plugin_Tester.exe` and select a VST3 plug-in or plug-in folder to test. Reports are written to the 125A Plugin Tester report directory under the current Windows user profile.

## Platform

- Windows x64
- VST3
- Standalone portable executable

## Version

125A Plugin Tester **0.2.0**

## License note

This project uses the Steinberg VST3 SDK under its applicable licensing terms. 125A project material is all rights reserved unless explicitly stated otherwise.
