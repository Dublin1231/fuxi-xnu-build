# Xiaomi 13 / fuxi XNU build investigation

This is an experimental source and macOS build workspace. **There is no bootable iOS image and no working hardware touch provider.** Production touch startup remains blocked.

The manually triggered **macOS XNU build investigation** workflow uses a standard `macos-26` runner. It reports the real Xcode/iphoneos SDK, runs the portable lifecycle/shared-resource/parser tests, then optionally attempts the pinned upstream baseline kernel build. If that build produces a real IOKit compile receipt, it replays the target compiler arguments for the two touch C++ adapters.

Results distinguish:

- Host tests on macOS.
- Baseline upstream kernel build outcome.
- Actual IOKit C++ compilation and ARM64/iOS object checks.
- Touch objects linked into a kernel: **not implemented in this first investigation**.
- Physical RPMh/GCC/ICC provider and device validation: **not implemented**.

No mock IOKit headers are used. A static archive or relocatable object is not accepted as a kernel link. This workflow has no phone/flash/partition steps, uploads no system images, and publishes only textual build status in its job summary. Native source files retain their SPDX identifiers. No personal backups, serial numbers, credentials or ROM binaries belong in this repository.

Pinned inputs:

| Source | Commit |
| --- | --- |
| UniXNU/UniXNU | `245aca1a9ec5b730b1cad80fade295800d2981f1` |
| hanzi123-bilp/unixnu-fuxi | `d12c0585c7c82941c17e4af6bace12dc46953c85` |
| apple-oss-distributions/xnu via upstream feeds | `d8b80295118ef25ac3a784134bcf95cd8e88109f` |

The initial fuxi config is console-only and does not enable UFS. Building it cannot demonstrate Apple userspace or daily use. Upstream may download external build inputs on the ephemeral runner; no derived kernel/system binaries are published here.

The remaining provider work requires qualified command DB data and an ownership/handoff contract at XNU takeover. Android runtime observations alone do not establish post-reboot resource ownership. Unknown shared-resource state must keep the provider unavailable.

Windows source checks: `python -m unittest discover -s ci -p 'test_*.py' -v`. Actual macOS execution and target compilation must be assessed from the Actions logs, not these planning tests.
