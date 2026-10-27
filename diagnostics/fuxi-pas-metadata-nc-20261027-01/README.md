# PAS metadata Normal-NC control (offline prepared)

Base: bangprovn/ios-fuxi@068faa44bcebb6b8a2292b6113a94ddb85214810, same kernel-metal4 build as V20. Stage existing 0111 logging plus new 0112 metadata mapping control after verifying pristine Git state, pinned adapter and every original kernel patch.

Only runtime change vs V20: pages covering PA 0x9ff600000 / 8192-byte metadata use VM_WIMG_WCOMB (Normal NC). All other window pages retain VM_WIMG_IO. At 16K granularity this affects one reserved 16K page; at 4K two pages. Compile-time guards bind the mapping constants to original G5_META_PA/G5_META_BYTES. No changes to PAS13, info82, SMC function/CPU/attempts, firmware signatures, metadata contents, stage16 stop, UFS, root, extent map, touch or boot arguments.

This is a hypothesis control, NOT a verified GPU repair or proof of EL2 access. Changing stage1 CPU attributes does not itself establish SHM/XPU permissions. No bridge enable/create/delete/ownership action is included.

45 host mapping cases and 16 unchanged Xiaomi metadata/PAS wrapper cases pass with ASan/UBSan. Host results do not prove full-kernel compilation or hardware behavior. Earlier host failures remain retained. Source-matched phone SCM/MDT module identities were verified separately but are PRIVATE: never upload those binaries, phone vendor_boot, boot images, root images, mappings or logs.

Workflow is manual-only, macos-14 + Xcode15.4, contents:read, kernel-metal4 only, artifact retention7d. This local package has NOT been published or dispatched. Previous one-build authorization was consumed by run40476374753; obtain an explicit new single-run authorization before any GitHub write/dispatch. Do not automatically rerun failures. No phone reboot is authorized by preparing this package.
