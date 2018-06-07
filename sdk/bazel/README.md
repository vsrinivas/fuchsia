Bazel SDK
=========

The Bazel SDK frontend produces a Bazel workspace.

# Directory structure

The contents of `base/` are copied verbatim into the output SDK. The
`generate.py` script then generates various files to produce a functional Bazel
repository.

# Output layout

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

# Testing

The `--debug` flag of the frontend script turns the generated repository into
an actual Bazel workspace where build commands can be run. This also copied the
contents of `tests/` into the workspace, so that smoke tests can be run with:
```
bazel build //tests/...
```

# Consuming

The produced Bazel SDK can be consumed by adding those lines to a Bazel
`WORKSPACE`:

```
http_repository(
  name = "fuchsia_sdk",
  path = "<FUCHSIA_SDK_URL>",
)

load("@fuchsia_sdk//build_defs:crosstool.bzl", "install_fuchsia_crosstool")
install_fuchsia_crosstool(
  name = "fuchsia_crosstool"
)
```

This adds the Fuchsia SDK to the workspace and sets up the necessary toolchains
for cross compilation against `x86_64` targets.

To reference the toolchains, add this to the .bazelrc file:

```
build:fuchsia --crosstool_top=@fuchsia_crosstool//:toolchain
build:fuchsia --cpu=x86_64
build:fuchsia --host_crosstool_top=@bazel_tools//tools/cpp:toolchain
```

Target can then be built for Fuschsia with:

```
$ bazel build --config=fuchsia //...
```
