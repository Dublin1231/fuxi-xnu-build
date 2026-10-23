# Xiaomi 13 / fuxi XNU build investigation

This is an experimental source and macOS build workspace. **There is no bootable iOS image and no working hardware touch provider.** Production touch startup remains blocked.

The manually triggered **macOS XNU build investigation** workflow uses a standard `macos-26` runner. It reports the real Xcode/iphoneos SDK, runs the portable lifecycle/shared-resource/parser tests, then optionally attempts the pinned upstream baseline kernel build. If that build produces a real IOKit compile receipt, it replays the target compiler arguments for the two touch C++ adapters.

Results distinguish:

- Host tests on macOS.
- Baseline upstream kernel build outcome.
- Actual IOKit C++ compilation and ARM64/iOS object checks.
- Touch objects linked into an executable kernel and prelink: separately verified by the binary audit.
- Physical RPMh/GCC/ICC provider and device validation: **not implemented**.

No mock IOKit headers are used. A static archive or relocatable object is not accepted as a kernel link. This workflow has no phone/flash/partition steps, uploads no system images, and publishes only textual build status in its job summary. Native source files retain their SPDX identifiers. No personal backups, serial numbers, credentials or ROM binaries belong in this repository.

Pinned inputs:

| Source | Commit |
| --- | --- |
| UniXNU/UniXNU | `245aca1a9ec5b730b1cad80fade295800d2981f1` |
| hanzi123-bilp/unixnu-fuxi | `d12c0585c7c82941c17e4af6bace12dc46953c85` |
| apple-oss-distributions/xnu via upstream feeds | `d8b80295118ef25ac3a784134bcf95cd8e88109f` |

The CI config is derived from the pinned console-only `fuxi_defconfig`. It disables the boot-progress dashboard because that feature requires audited UFS/owner-root inputs absent from this configuration. A documented overlay guards the optional legacy-display registration call with the existing `XNU_NATIVE_LEGACY_DISPLAY` flag; it supplies no dummy implementation. Original upstream config remains unchanged, and overlay hashes are recorded in the job workspace.

This diagnostic build does not enable UFS and cannot demonstrate Apple userspace or daily use. Upstream may download external build inputs on the ephemeral runner; no derived kernel/system binaries are published here.

The first run failed at the inherited legacy-display link reference, the incompatible boot-progress configuration, and receipt macro recognition. Clang receipts can serialize `-DKERNEL` as `-D`, `KERNEL`; the parser now recognizes both exact forms while retaining all compiler arguments and still rejecting non-kernel receipts.

Run 39976021256 compiled both touch C++ adapters into ARM64/iOS objects using the real XNU receipt. Its baseline still failed because the upstream configuration enabled source crypto without enabling archive builders. The CI overlay now enables actual archive construction for corecrypto (plain and namespaced), ArmDTPlatform, ArmCpu, NativeAES, AES bindings and pthread. It excludes builders for disabled UFS, dashboard and legacy display, and removes the unused legacy stub link input. It does not substitute dummy crypto or mark a failed baseline successful. Builder order and feature assumptions are checked against the pinned board before modification.

The remaining provider work requires qualified command DB data and an ownership/handoff contract at XNU takeover. Android runtime observations alone do not establish post-reboot resource ownership. Unknown shared-resource state must keep the provider unavailable.

Windows source checks: `python -m unittest discover -s ci -p 'test_*.py' -v`. Actual macOS execution and target compilation must be assessed from the Actions logs, not these planning tests.

The next investigation also compiles the eight native C dependency files plus the pinned upstream GENI SPI implementation using a genuine XNU C compile receipt. It force-loads these and the two C++ adapters into a newly linked executable kernel, repeats prelink, and checks retained definitions, new unresolved imports, C++ constructor pointers and the constant `ST_BLOCKED` production gate in the actual binary. Host-only start and mock transport objects are excluded. Constructors can register the two helper classes; there is still no IOService matching personality or hardware provider. The earlier `NativeHidInput.fuxi.patch` depends on an independently qualified private HID device ABI and is not included or claimed as integrated here. No HID service, IRQ or hardware operation is instantiated by this build.
