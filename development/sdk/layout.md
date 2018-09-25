# SDK layout

This document describes the standard layout of a Fuchsia SDK.

```
$root/
    tools/                         # host tools
    pkg/                           # arch-independent package contents
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
            sysroot/
                include/
                lib/
                dist/
                debug/
            lib/
                libfoo.so          # ABI only, to link against
            dist/
                libfoo.so          # to include in Fuchsia packages
            debug/
                libfoo.so          # unstripped versions
        arm64/
            sysroot/
                include/
                lib/
                dist/
                debug/
            lib/
            dist/
            debug/
    target/                        # target-dependent prebuilts
        x64/
            fuchsia.zbi
        arm64/
            fuchsia.zbi
```
