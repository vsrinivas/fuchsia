Bazel SDK
=========

The Bazel SDK frontend produces a Bazel workspace.

# Directory structure

- `generate.py`: the script that generates the SDK;
- `templates`: Mako templates used to produce various SDK files;
- `base`: SDK contents that are copied verbatim;
- `generate-tests.py`: script that create a test workspace;
- `tests`: various SDK tests, copied over to the test workspace.

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

The `generate-tests.py` script creates a workspace for testing the generated
SDK. From within that workspace, run:
```
bazel build --config=fuchsia //...
```

# Consuming

The produced Bazel SDK can be consumed by adding those lines to a Bazel
`WORKSPACE`:

```
http_archive(
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
