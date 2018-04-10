# SDK layout

This document describes the standard layout of a Fuchsia Bazel SDK.

```
$root/
    tools/                                 # host tools
    pkg/                                   # package contents
        system/                            # libc, libzircon, and friends
            include/
            arch/                          # prebuilt binary artifacts
            BUILD                          # generated Bazel build file for this package
        foo/
            include/                       # headers
            docs/                          # documentation
            meta/                          # metadata, e.g. build plan
            arch                           # target-independent prebuilts
                x64/
                    lib/
                        libfoo.so          # ABI only, to link against
                    dist/
                        libfoo.so          # to include in Fuchsia packages
                    debug/
                        libfoo.so          # unstripped versions
                arm64/
                    lib/
                    dist/
                    debug/
            BUILD
        bar/
            include/
            src/                           # sources for a C++ library
            docs/
            meta/
            BUILD
```
