# Build Fuchsia with a custom Rust toolchain

This guide explains how to build a Rust compiler for use with the Fuchsia
build and configure a Fuchsia build to use it. This is useful if you need to
build Fuchsia with a patched compiler, or a compiler built with custom
options.

### Use another version of Rust

If all you need to do is use a different version than the one currently being
used, most of this doc is not necessary. Fuchsia builders [build Rust] after
every change to Rust's main branch.

1. Find the commit hash you want to use.
2. Run the following commands from your Fuchsia directory:

   ```posix-terminal
   # Replace COMMIT with the full Rust commit hash.
   # This command updates the manifests in the integration repo, which you can
   # then commit or revert as necessary.
   fx roll-compiler --package rust git_revision:{{ '<var>' }}COMMIT{{ '</var>' }}

   # Fetch the package versions you specified and install them in `prebuilt/`.
   jiri fetch-packages -local-manifest
   ```

3. Run the following command to build Fuchsia:

   ```posix-terminal
   fx build
   ```

   The Fuchsia build now uses the updated compiler.

   Note: A clean build is not necessary; the build automatically detects
   the new compiler version.

[build Rust]: https://ci.chromium.org/p/fuchsia/g/rust/console

## Prerequisites

Prior to building Fuchsia with a custom Rust toolchain, you need to do the following:

Note: These instructions are for Debian-based systems, but you should use the
correct package manager for your machine.

1. Run the following command to install cmake:

   ```posix-terminal
   sudo apt-get install cmake ninja-build
   ```

1. Complete the following guide to download the Fuchsia source:
   [Get Fuchsia source code](/docs/get-started/get_fuchsia_source.md).
   To confirm that jiri is in your PATH run <code>jiri -help</code>.

   Note: The below commands assume `DEV_ROOT` is set to the parent directory of
   your Fuchsia checkout.

1. Run the following command to obtain the infra sources:

   ```posix-terminal
   DEV_ROOT={{ '<var>' }}DEV_ROOT{{ '</var> '}} # parent of your fuchsia directory
   mkdir -p $DEV_ROOT/infra && \
   ( \

     builtin cd $DEV_ROOT/infra && \
     jiri init && \
     jiri import -overwrite -name=fuchsia/prebuilt infra \
         https://fuchsia.googlesource.com/manifest && \
     jiri update \
   )
   ```

   Note: Running `jiri update` from the `infra` directory ensures that you
   have the most recent configurations and tools.

1. Download and extract the Fuchsia core IDK to `$DEV_ROOT/sdk`. For more
   information, see [Download the Fuchsia IDK](/docs/development/idk/download.md).

1. Run the following command to use `cipd` to get the linux `sysroot` package
   for your host platform:

   ```posix-terminal
   # You may want to: rm -rf $DEV_ROOT/sysroot
   mkdir -p $DEV_ROOT/sysroot
   cipd install fuchsia/sysroot/linux-amd64 latest -root $DEV_ROOT/sysroot/linux-x64
   cipd install fuchsia/sysroot/linux-arm64 latest -root $DEV_ROOT/sysroot/linux-arm64
   ```

1. If you haven't already, clone the Rust source.

   ```posix-terminal
   git clone https://github.com/rust-lang/rust.git $DEV_ROOT/rust
   ```

## Configure Rust for Fuchsia

1. Change into your Rust directory.
1. Run the following command to generate a configuration for the Rust toolchain:

   ```posix-terminal
   DEV_ROOT={{ '<var>' }}DEV_ROOT{{ '</var>' }}

   $DEV_ROOT/infra/fuchsia/prebuilt/tools/vpython \
     $DEV_ROOT/infra/fuchsia/recipes/recipes/contrib/rust_toolchain.resources/generate_config.py \
       config_toml \
       --clang-prefix=$DEV_ROOT/fuchsia/prebuilt/third_party/clang/linux-x64 \
       --host-sysroot=$DEV_ROOT/fuchsia/prebuilt/third_party/sysroot/linux \
       --prefix=$(pwd)/install/fuchsia-rust \
      | tee fuchsia-config.toml

   $DEV_ROOT/infra/fuchsia/prebuilt/tools/vpython \
       $DEV_ROOT/infra/fuchsia/recipes/recipes/contrib/rust_toolchain.resources/generate_config.py \
         environment \
         --eval \
         --clang-prefix=$DEV_ROOT/fuchsia/prebuilt/third_party/clang/linux-x64 \
         --sdk-dir=$DEV_ROOT/sdk \
         --linux-amd64-sysroot=$DEV_ROOT/sysroot/linux-x64 \
         --linux-arm64-sysroot=$DEV_ROOT/sysroot/linux-arm64 \
      | tee fuchsia-env.sh
   ```

1. (Optional) Run the following command to tell git to ignore the generated files:

   ```posix-terminal
   echo fuchsia-config.toml >> .git/info/exclude
   echo fuchsia-env.sh >> .git/info/exclude
   ```

1. (Optional) Customize `fuchsia-config.toml`.

## Build and install Rust

1. Change into your Rust source directory.
1. Run the following command to build and install Rust plus the Fuchsia runtimes spec:

   ```posix-terminal
   DEV_ROOT={{ '<var>' }}DEV_ROOT{{ '</var>' }}

   rm -rf install/fuchsia-rust
   mkdir -p install/fuchsia-rust

   # Copy and paste the following subshell to build and install Rust, as needed.
   # The subshell avoids polluting your environment with fuchsia-specific rust settings.
   ( source fuchsia-env.sh && ./x.py install --config fuchsia-config.toml ) && \
   rm -rf install/fuchsia-rust/lib/.build-id && \
   $DEV_ROOT/infra/fuchsia/prebuilt/tools/vpython \
     $DEV_ROOT/infra/fuchsia/recipes/recipes/contrib/rust_toolchain.resources/generate_config.py \
       runtime \
     | $DEV_ROOT/infra/fuchsia/prebuilt/tools/vpython \
         $DEV_ROOT/infra/fuchsia/recipes/recipe_modules/toolchain/resources/runtimes.py \
           --dir install/fuchsia-rust/lib \
           --readelf fuchsia-build/*/llvm/bin/llvm-readelf \
           --objcopy fuchsia-build/*/llvm/bin/llvm-objcopy \
     > install/fuchsia-rust/lib/runtime.json
   ```

### Build only (optional)

If you want to skip the install step, for instance during development of Rust
itself, you can do so with the following command.

```posix-terminal
( source fuchsia-env.sh && ./x.py build --config fuchsia-config.toml )
```

### Troubleshooting

If you are getting build errors, try deleting the Rust build directory:

```posix-terminal
rm -rf fuchsia-build
```

Then re-run the command to build Rust.

## Build Fuchsia with your custom Rust toolchain

1. Change into your Fuchsia directory.

1. Run the following command to use the newly built toolchain:

   ```posix-terminal
   DEV_ROOT={{ '<var>' }}DEV_ROOT{{ '</var>' }}

   fx set core.x64 \
     --args=rustc_prefix="\"$DEV_ROOT/rust/install/fuchsia-rust/bin\"" \
     --args=rustc_version_string='"1"'
   # plus other settings such as:
   #   --with //bundles:kitchen_sink
   #   --variant=coverage-rust  # to enable coverage profiling of fuchsia binaries
   #   --variant=host_coverage-rust  # to enable coverage profiling of host binaries
   ```

   Note: `rustc_version_string` can be any string, and it’s used to force a
   recompile after a custom toolchain changes. If you rebuild the toolchain,
   change the value so Rust targets get rebuilt.

1. Run the following command to rebuild Fuchsia:

   ```posix-terminal
   fx build
   ```
