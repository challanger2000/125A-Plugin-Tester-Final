# CURRENT — 125A Plugin Tester v0.2.10 FINAL

**Product class:** standalone internal Windows x64 VST3 QA utility; not a VST3 plug-in and not a Gumroad FULL product.

**Final version:** `v0.2.10` — published GitHub Release: https://github.com/challanger2000/125A-Plugin-Tester-Final/releases/tag/v0.2.10

**Exact tested source commit / release tag target:** `ec2db65b9b9dbce2b7a5d146cb54869c9ad82e6d`.

**Final Windows QA:** SUCCESS, https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/38744097119

**Release files:**
- `125A_Plugin_Tester.exe`, 2,204,160 bytes, SHA-256 `b0f386422d107ac6fa8b15c3e1e86113f52b013e277fc94b16d28b929438bfea`.
- `125A_Plugin_Tester_v0.2.10_Windows_x64_FINAL.zip`, SHA-256 `e5b152fa5b05140f5dfc883c7dd736f572ca50c13b78d90be7505ec6e13e83af`.

**Release QA:** Windows build, Steinberg validator selftest, guard selftest, dispatcher classification and write errors, 16 positive/negative VST3 fixtures, isolated 100-profile 32/64-bit sample-rate/block-size matrix, actual audio and state/bypass, event/transport, editor/audio concurrency, host-paced timing, callable imported Windows heap/lock API tracing, folder scan and portable EXE end-to-end.

**Scope of the last instrument:** Direct imported Win32 HeapAlloc/HeapReAlloc/VirtualAlloc and EnterCriticalSection/WaitForSingleObject/WaitForSingleObjectEx calls can be observed while the plugin's `process()` runs. The monitor does NOT prove full freedom from custom/static allocator activity, dynamically resolved APIs or every internal lock. Calls to potentially blocking APIs do not by themselves prove contention. Uninstrumented paths must not be represented as confirmed clean.

**Previous stable v0.2.9** remains on default `main`; no customer package was overwritten. The immutable tagged source and binary GitHub Release are the authoritative v0.2.10 FINAL, not branch HEAD or a temporary Actions artifact.
