# Fuchsia Build System

These are notes on using the `gen.py` script and `ninja` directly for builds,
and the `scripts/run-zircon-*` scripts to launch QEMU.

You can alternatively use the [standard build instructions](/getting_started.md#Setup-Build-Environment),
which use the `fx` commands instead.

### Build Zircon and the sysroot

First, you need to
[build the Zircon kernel](https://fuchsia.googlesource.com/zircon/+/master/docs/getting_started.md)
and the sysroot:

```
./scripts/build-zircon.sh
```

### Build Fuchsia

Build Fuchsia using these commands:

```
./build/gn/gen.py
./buildtools/ninja -C out/debug-x86-64
```

Optionally if you have ccache installed and configured (i.e. the CCACHE_DIR
environment variable is set to an existing directory), use the following command
for faster builds:

```
./build/gn/gen.py --ccache
./buildtools/ninja -C out/debug-x86-64
```

[Googlers only] If you have goma installed, prefer goma over ccache and use
these alternative commands for faster builds:

```
./build/gn/gen.py --goma
./buildtools/ninja -j1024 -C out/debug-x86-64
```

The gen.py script takes an optional parameter '--target\_cpu' to set the target
architecture. If not supplied, it defaults to x86-64.

```
./build/gn/gen.py --target_cpu=aarch64
./buildtools/ninja -C out/debug-aarch64
```

You can configure the set of packages that `gen.py` uses with the `--packages`
argument. After running `gen.py` once, you can do incremental builds using
`ninja`.

You can configure how specific components will be built using `--variant`
arguments.  [See here](build_variants.md) for details.

For a list of all `gen.py` options, run `gen.py --help`.

### Running on ARM Hardware

To build a fuchsia image for a particular ARM hardware target, you must also
provide the zircon project name for that particular target to gen.py via the
zircon_project argument. For example, to build a fuchsia image for the
HiKey 960, you might invoke gen.py as follows:

```
./build/gn/gen.py --zircon_project=zircon-hikey960-arm64 --target_cpu=aarch64
./buildtools/ninja -C out/debug-aarch64
```
