# Zircon Toolchain Prebuilts

These prebuilts were built automatically on the Fuchsia build infrastructure.

The source used to generate these objects can be found at https://fuchsia.googlesource.com/third_party/gcc_none_toolchains
at the SHA that is the package filename.  The SHA will also appended to the end of this file.

These tools are built by a combination of upstream sources and patches.  The patches
can be found in the [gcc_none_toolchains](https://fuchsia.googlesource.com/third_party/gcc_none_toolchains) repo and the
upstream sources have been mirrored to Google Storage for posterity.

List all the upstream sources:
gsutil ls gs://fuchsia-build/zircon/toolchain/sources/

You can download them with HTTPS at addresses with the form of:
https://fuchsia-build.storage.googleapis.com/zircon/toolchain/sources/[package name]
(E.g. https://fuchsia-build.storage.googleapis.com/zircon/toolchain/sources/gcc-5.3.0.tar.bz2)

# Git SHA used to build this package:
