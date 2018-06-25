# Bazel SDK

The Bazel SDK frontend produces a [Bazel](https://bazel.build/) workspace.

## Directory structure

- `generate.py`: the script that generates the SDK;
- `templates`: Mako templates used to produce various SDK files;
- `base`: SDK contents that are copied verbatim;
- `generate-tests.py`: a script that creates a test workspace;
- `tests`: various SDK tests, copied over to the test workspace.

## Output layout

```
$root/
    tools/                                 # host tools
    dart/                                  # Dart packages
        lorem/
            BUILD
            lib/
    pkg/                                   # C++ package contents
        foo/
            BUILD                          # generated Bazel build file for this package
            include/                       # headers
            arch                           # target-independent prebuilts
                x64/
                    lib/
                        libfoo.so          # ABI only, to link against
                    dist/
                        libfoo.so          # to include in Fuchsia packages
                    debug/
                        libfoo.so          # unstripped version
                arm64/
                    lib/
                    dist/
                    debug/
            BUILD
        bar/
            include/
            src/                           # sources for a C++ library
            BUILD
    arch/
        x64/
            sysroot/                       # x64 sysroot (libc, libzircon, and friends)
        arm64/
            sysroot/                       # arm64 sysroot
```

## Testing

The `generate-tests.py` script creates a workspace for testing the generated
SDK. From within that workspace, run:
```
$ ./run.py
```

To exclude a target from the suite, mark it as ignored with:
```
my_rule(
    name = "foobar",
    ...
    tags = [
        "ignored",
    ],
)
```
To force-build ignored targets, use the `--ignored` flag.

The test runner also builds targets in the SDK itself. To bypass this step, use
the `--no-sdk` flag.

## Consuming

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
for cross compilation.

To reference the toolchains, add this to the .bazelrc file:

```
build:fuchsia --crosstool_top=@fuchsia_crosstool//:toolchain
build:fuchsia --cpu=x64
build:fuchsia --host_crosstool_top=@bazel_tools//tools/cpp:toolchain
```

Targets can then be built for Fuschsia with:

```
$ bazel build --config=fuchsia //...
```
