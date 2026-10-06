# 125A Plugin Tester / Quality Checker

**CURRENT FINAL SOURCE: v0.2.9 — this is the authoritative Final repository state.**

Console-based VST3 quality checker for Windows x64. It is designed for 125A internal QA and for general technical checks of third-party VST3 plug-ins.

## Current QA coverage

- VST3 module loading and factory/class enumeration
- component and controller creation/initialization
- bidirectional `IConnectionPoint` connection where supported
- audio/event bus metadata and default-bus activation
- host-correct `ProcessData` bus ordering, including inactive audio buses
- reversible bus activation/deactivation checks without treating unsupported optional transitions as automatic failures
- `IAudioProcessor::getBusArrangement()` consistency against `BusInfo` channel counts
- invalid audio-bus arrangement index rejection
- current-layout arrangement roundtrip checks
- controlled mono/stereo alternate-layout requests; accepted changes must be truthful and reversible
- auxiliary audio input/sidechain discovery and isolated processing of every exposed auxiliary input
- dense sample-accurate event stress with NoteOn/NoteOff pairs across supported event channels, including offsets 0 and 511
- validation of returned event offsets where event output buses are exposed
- parameter metadata, unique IDs, ranges and step counts
- VST3 bypass-parameter metadata
- normalized/plain parameter conversion stress
- component state save/restore roundtrip
- controller component-state synchronization
- controller private-state roundtrip where available
- isolated fresh-instance component-state transfer and re-save verification
- fresh-instance controller `setComponentState()` check where a controller is available
- `IAudioProcessor` availability, latency and tail reporting
- 32-bit / 64-bit sample-format capability checks
- `setupProcessing()` matrix at 44.1 / 48 / 88.2 / 96 / 192 kHz
- block sizes from 1 to 8192 samples
- real `process()` lifecycle checks
- silence, impulse, DC and denormal input tests where applicable
- native float/double subnormal detection
- NaN / Infinity output detection
- output peak / RMS / DC statistics
- sustained processing stress (256 blocks / 131072 samples per supported sample format)
- sample-accurate automation stress through `IParameterChanges`
- real bypass ON/OFF processing stress where a VST3 bypass parameter is exported
- repeated activate/process/deactivate lifecycle stress
- offline-processing setup and process checks
- post-stress component-state restoration
- isolated repeated module reload / component / controller instantiation probe
- official Steinberg VST3 validator from the same pinned SDK revision
- full Steinberg validator diagnostics written to `<Plugin>_125A_Steinberg_Validator.txt`
- reload/instantiation, fresh-instance state, I/O/event and Steinberg validator results integrated into the final QA report
- stale QA/guard reports are removed before a new test; the launcher refuses to start if an old report cannot be cleared
- PASS / WARN / FAIL classification
- automatic `<Plugin>_125A_QA_Report.txt` report
- guard report for launcher/probe/worker/validator crashes and hangs

## Crash / hang isolation

The Windows package now contains seven executables:

- `125A_Plugin_Tester.exe` - user-facing launcher
- `125A_Plugin_Tester_ReloadProbe.exe` - isolated repeated reload/instantiation probe
- `125A_Plugin_Tester_StateProbe.exe` - isolated fresh-instance state-transfer probe
- `125A_Plugin_Tester_IOEventProbe.exe` - isolated bus-layout, sidechain and event stress probe
- `125A_Plugin_Tester_SteinbergValidator.exe` - 125A wrapper that normalizes Steinberg validator exit semantics and captures diagnostics
- `125A_Plugin_Tester_SteinbergValidatorCore.exe` - unmodified official Steinberg validator built from VST3 SDK `v3.8.1_build_84`
- `125A_Plugin_Tester_Worker.exe` - isolated full 125A QA worker

Keep all seven files in the same folder. Normal users only start `125A_Plugin_Tester.exe`.

Every isolated stage is started suspended, assigned to a Windows Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, and only then resumed. A timeout terminates the complete Job Object rather than only the immediate child process. Closing the Job Object after a completed stage also prevents descendant processes from being left behind.

The launcher runs the reload/instantiation probe, fresh-instance state probe, I/O/sidechain/event probe, official Steinberg validator, and full 125A worker. The Steinberg validator gets a longer timeout because its conformance suite can take substantially longer than a single internal probe.

The state probe saves component state from one initialized instance, applies it to a separately initialized instance of the same VST3 class, asks the new instance to serialize state again, and checks controller component-state synchronization where available. Raw state bytes are deliberately not required to be byte-identical because valid plug-ins may use non-canonical serialization.

The I/O/event probe checks bus metadata and reversible activation behavior, validates audio bus arrangements and invalid indices, conditionally exercises supported mono/stereo alternate arrangements, processes all exposed auxiliary audio inputs one at a time, and sends a dense sample-accurate note-event block through an available event input. Optional layout/activation requests that are legitimately rejected are not automatically classified as plug-in defects; an accepted request must, however, be reflected consistently and be reversible.

## Tester self-validation

The Windows CI build compiles an internal guard self-test helper that deliberately produces normal exit, deterministic failure, access violation and hang scenarios. Instead of testing those primitives separately, CI calls a hidden launcher self-test mode. Those four cases therefore pass through the same production `runIsolated()` Job Object supervisor used for real plug-ins. The internal helper is explicitly excluded from the release artifact.

CI also runs the official Steinberg validator's own `-selftest` before the package is staged.

In addition, CI builds four internal VST3 fixtures and sends all of them through the real production launcher:

- `125A_QA_GoodControl.vst3` must be accepted, guarding against false positives
- `125A_QA_BadLifecycle.vst3` deliberately rejects `setProcessing(true)` and must fail
- `125A_QA_BadNaN.vst3` deliberately emits NaN audio and must fail
- `125A_QA_BadState.vst3` deliberately rejects state restoration and must fail

These fixtures are test infrastructure only and are never installed or shipped. A package is not produced unless the production Job Object guard, Steinberg validator self-test, good-control specificity check and all three defect-detection checks behave exactly as expected.

## Usage

Run:

```text
125A_Plugin_Tester.exe "C:\Program Files\Common Files\VST3\YourPlugin.vst3"
```

or start `125A_Plugin_Tester.exe` without arguments and paste the full `.vst3` path.

## Interpretation

`PASS` means the tested behavior completed successfully under the current test suite. `WARNING` means the result needs review or may represent unsupported/special host behavior. `FAIL` means a tested contract or processing operation failed. A result is not a blanket judgment of a vendor or product; findings should be reproduced and classified before publication.

The official Steinberg validator is treated as a separate conformance layer. A successful validator run adds a PASS to the final report; a deterministic validator failure adds a FAIL. A validator crash, timeout, launch error or unexpected exit is treated as tester/infrastructure inconclusive rather than silently being blamed on the plug-in. The validator's complete textual diagnostics are retained in a separate sidecar report so a failed Steinberg test can be identified exactly instead of being reduced to a generic FAIL.

## Build

The GitHub Actions workflow is manual (`workflow_dispatch`) to avoid unnecessary CI usage. It builds Windows x64 with Visual Studio 2022 and VST3 SDK `v3.8.1_build_84`, runs the production Job Object guard self-test, runs the Steinberg validator self-test, exercises the complete production tester against deterministic good and deliberately faulty VST3 fixtures, and only then stages the seven release executables.

## Later extensions

The remaining larger hardening items include JSON/CI output, configurable per-stage timeouts, signed Windows releases, and a small serious-purpose GUI after the test engine itself is fully validated.
