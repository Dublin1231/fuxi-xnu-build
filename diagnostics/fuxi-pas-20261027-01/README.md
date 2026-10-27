# PAS diagnostic build inputs �� prepared, not dispatched

Use only a pristine bangprovn/ios-fuxi checkout at the source-lock commit.
On a Mac with Xcode 15.4, run stage-pas-diagnostics.py CHECKOUT --stage,
then inside CHECKOUT run bash tools/ci/build.sh kernel-metal4.
The existing builder may download public source and install its pinned KDK.
The optional workflow requires explicit user authorization before publishing
or running. Runner/Xcode availability must be checked again before dispatch.
Only the last logging patch is added; no firmware, boot.img, root image,
phone serial, rootslot map, phone logs or credentials are packaged.
Full kernel compilation has NOT been tested by this Windows preparation.
The existing kernel patch chain plus the final diagnostic patch was replayed
locally against the real driver; its result matches the qualified diag source.
Generated CI ADT/cmdline are PUBLIC build outputs, not the device mapping.
Do not use them to boot the phone; repack locally with the retained current
ADT, stage16 args, V19 firmware and rootslot mapping after binary qualification.
