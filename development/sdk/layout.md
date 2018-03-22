# SDK layout

This document describes the standard layout of a Fuchsia SDK.

```
$root/
    tools/                         # host tools
    pkg/                           # arch-independent package contents
        system/                    # libc, libzircon, and friends
            include/
        foo/
            include/               # headers
            docs/                  # documentation
            meta/                  # metadata, e.g. build plan
        bar/
            include/
            src/                   # sources for a C++ library
            docs/
            meta/
    arch                           # target-independent prebuilts
        x64/
            lib/
                libzircon.so
                libfoo.so          # ABI only, to link against
            dist/
                libfoo.so          # to include in Fuchsia packages
            debug/
                libzircon.so
                libfoo.so          # unstripped versions
        arm64/
            lib/
            dist/
            debug/
    target/                        # target-dependent prebuilts
        x64/
            zircon.bin
            bootdata-pc.bin
        arm64/
            zircon.bin
            bootdata-gauss.bin
            bootdata-hikey960.bin
```
