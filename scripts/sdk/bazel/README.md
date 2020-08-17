# Bazel SDK

The Bazel SDK frontend produces a [Bazel](https://bazel.build/) workspace.

> **This Bazel SDK is not meant for public use!**

This Bazel SDK is used to verify that the core Integrator's Developer Kit (IDK)
is not biased towards any particular build system. However, this code is not
actively maintained, so it may break when used with future versions of its
dependencies.

If you are looking for an SDK to use, check out the
[GN SDK](https://fuchsia.dev/fuchsia-src/development/sdk/gn), or consider
building your own using the
[IDK](https://fuchsia.dev/fuchsia-src/development/sdk).


## Directory structure

- `generate.py`: the script that generates the SDK;
- `templates`: Mako templates used to produce various SDK files;
- `base`: SDK contents that are copied verbatim;
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

## Generating

In order to generate a Bazel workspace, point the `generate.py` script to an
SDK archive, e.g.:
```
$ scripts/sdk/bazel/generate.py \
    --archive my_sdk_archive.tar.gz \
    --output my_workspace/
```

## Testing

The `generate.py` script optionally creates a workspace for testing the
generated SDK:
```
$ scripts/sdk/bazel/generate.py \
    --archive my_sdk_archive.tar.gz \
    --output my_workspace/ \
    --tests my_test_workspace/
```

Tests are then run with:
```
$ my_test_workspace/run.py
```

It is recommended to use the version of Bazel available in the Fuchsia source
tree at `//prebuilt/sdk/bazel` to run the tests:
```
$ my_test_workspace/run.py --bazel $FUCHSIA_DIR/prebuilt/sdk/bazel/bazel
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

### C++

The produced Bazel SDK can be consumed by adding those lines to a Bazel
`WORKSPACE`:

```
http_archive(
  name = "fuchsia_sdk",
  path = "<FUCHSIA_SDK_URL>",
)

load("@fuchsia_sdk//build_defs:fuchsia_setup.bzl", "fuchsia_setup")
fuchsia_setup(with_toolchain = True)
```

This adds the Fuchsia SDK to the workspace and sets up the necessary toolchains
for cross compilation.

To reference the toolchains, add this to the .bazelrc file:

```
build:fuchsia --crosstool_top=@fuchsia_crosstool//:toolchain
build:fuchsia --cpu=x86_64
build:fuchsia --host_crosstool_top=@bazel_tools//tools/cpp:toolchain
```

Targets can then be built for Fuchsia with:

```
$ bazel build --config=fuchsia //...
```

### Dart & Flutter

To build Dart & Flutter packages using the Bazel SDK, add those lines to the
Bazel `WORKSPACE`:

```
http_archive(
  name = "fuchsia_sdk",
  path = "<FUCHSIA_SDK_URL>",
)

load("@fuchsia_sdk//build_defs:fuchsia_setup.bzl", "fuchsia_setup")
fuchsia_setup(with_toolchain = False)

http_archive(
  name = "io_bazel_rules_dart",
  url = "https://github.com/dart-lang/rules_dart/archive/master.zip",
  strip_prefix = "rules_dart-master",
)

load("@io_bazel_rules_dart//dart/build_rules:repositories.bzl", "dart_repositories")
dart_repositories()

load("@fuchsia_sdk//build_defs:setup_dart.bzl", "setup_dart")
setup_dart()

load("@fuchsia_sdk//build_defs:setup_flutter.bzl", "setup_flutter")
setup_flutter()
```
