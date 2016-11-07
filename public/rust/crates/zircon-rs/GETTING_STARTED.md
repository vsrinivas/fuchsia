Getting started with Rust on Fuchsia
====================================

This page contains instructions for how to obtain a Rust toolchain
that can be used to build binaries suitable for running on Fuchsia.

Building the compiler
---------------------

First, build the [clang wrapper](tools/). It helps if you check out this repo
under your fuchsia root, but it's not necessary. We'll call the directory
where you created the symlinks `${RUST_TOOLS}`. In any case, the created
symlinks do need to be under your Fuchsia root.

You can sanity-check the clang wrapper:

```
${RUST_TOOLS}/x86-64-unknown-fuchsia-cc 
clang-4.0: error: no input files
```

Check out a fresh copy of the Rust compiler:

```
git clone https://github.com/rust-lang/rust.git
cd rust
```

Henceforth, we'll call this directory `${RUST_ROOT}`.

Create a config.toml file (in the `${RUST_ROOT}`) from the following template.
Expand the `${RUST_TOOLS}` paths out to the actual absolute path.

```
# Config file for fuchsia target for building Rust.
# See `src/bootstrap/config.toml.example` for other settings.

[rust]

# Disable backtrace, as it requires additional lib support.
backtrace = false

[target.x86_64-unknown-fuchsia]

cc = "${RUST_TOOLS}/x86-64-unknown-fuchsia-cc"

[target.aarch64-unknown-fuchsia]

# Path to the clang wrapper
cc = "${RUST_TOOLS}/aarch64-unknown-fuchsia-cc"
```

Run:

```
./configure --enable-rustbuild --target=x86_64-unknown-fuchsia
./x.py build --stage 1 --target=x86_64-unknown-fuchsia
```

You should now have a working Rust compiler under `build/x86_64-apple-darwin/stage1/bin/rustc`
(adjust your host triple as necessary).

You can adjust exactly what gets built using the --stage and --step flags to `bootstrap.py`.
If you're building a compiler for release, you'll want the stage2 compiler. Conversely,
if you're hacking on the compiler or libraries, you might want to build less to reduce
your edit-compile-test cycle (for example `--step libstd`).

Building code with rustc and cargo
----------------------------------

To build a single binary, run:

```
./build/x86_64-apple-darwin/stage1/bin/rustc --target x86_64-unknown-fuchsia hello.rs \
  -Clinker=${RUST_TOOLS}/x86-64-unknown-fuchsia-cc
```

To build with cargo, create a .cargo/config. As above, expand the absolute path.

```
[target.x86_64-unknown-fuchsia]
linker = "${RUST_TOOLS}/x86-64-unknown-fuchsia-cc"

[target.aarch64-unknown-fuchsia]
linker = "${RUST_TOOLS}/aarch64-unknown-fuchsia-cc"
```

Then, invoke cargo as follows:

```
RUSTC=${RUST_ROOT}/build/x86_64-apple-darwin/stage1/bin/rustc cargo build --target=x86_64-unknown-fuchsia --example mx_toy
```

This will produce a binary as `target/x86_64-unknown-fuchsia/debug/examples/mx_toy`.

Running binaries on Fuchsia
---------------------------

First, of course, you'll need [Fuchsia](https://fuchsia.googlesource.com/fuchsia/).
You probably already have this if your rust wrappers work.

There are a few ways to get binaries into the file system (and this is rapidly
evolving to become more flexible), but the most straightforward is to manually
edit the manifest for creating the bootfs image.

Edit `out/debug-x86-64/gen/packages/gn/user.bootfs.manifest` and manually add a line like:

```
bin/mx_toy=${YOUR_CRATE_ROOT}/target/x86_64-unknown-fuchsia/debug/examples/mx_toy
```

Then, rebuild the bootfs (in your fuchsia root as cwd):

```
./out/build-magenta/tools/mkbootfs -o ./out/debug-x86-64/user.bootfs out/debug-x86-64/gen/packages/gn/user.bootfs.manifest
```

Now you can start the system, and run your binary:

```
cd magenta
./scripts/run-magenta-x86-64 -g -x ../out/debug-x86-64/user.bootfs
mx_toy
```

Running tests
-------------

Running tests is possible but somewhat clunky.

First, build your test executable:

```
RUSTC=${RUST_ROOT}/build/x86_64-apple-darwin/stage1/bin/rustc cargo test --target=x86_64-unknown-fuchsia --no-run
```

This will generate a test executable with a name like
`target/x86_64-unknown-fuchsia/debug/magenta-523728a3f134326a`. Run this
binary as above.

Notes
-----

General instructions on cross-compiling Rust can be found at
[japaric/rust-cross](https://github.com/japaric/rust-cross). At some point soon
it will be possible to build fuchsia targets using nightly Rust (and then stable
at the appropriate time), but it still requires a `std` crate for your target.
Prebuilt versions are not yet available.

