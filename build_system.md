# Fuchsia Build Notes

These are notes on using the `gen.py` script and `ninja` directly for builds,
and the `scripts/run-magenta-*` scripts to launch QEMU.

You can alternatively use the [standard build instructions](https://fuchsia.googlesource.com/docs/+/master/getting_started.md#Setup-Build-Environment)
using commands defined in `env.sh` instead.

### Build Magenta and the sysroot

First, you need to
[build the Magenta kernel](https://fuchsia.googlesource.com/magenta/+/master/docs/getting_started.md)
and the sysroot:

```
(cd magenta; make -j32 magenta-pc-x86-64)
./scripts/build-sysroot.sh
```

### Build Fuchsia

Build Fuchsia using these commands:

```
./packages/gn/gen.py
./buildtools/ninja -C out/debug-x86-64
```

Optionally if you have ccache installed and configured (i.e. the CCACHE_DIR
environment variable is set to an existing directory), use the following command
for faster builds:

```
./packages/gn/gen.py --ccache
./buildtools/ninja -C out/debug-x86-64
```

[Googlers only] If you have goma installed, prefer goma over ccache and use
these alternative commands for faster builds:

```
./packages/gn/gen.py --goma
./buildtools/ninja -j1000 -C out/debug-x86-64
```

The gen.py script takes an optional parameter '--target\_cpu' to set the target
architecture. If not supplied, it defaults to x86-64.

```
./packages/gn/gen.py --target_cpu=aarch64
./buildtools/ninja -C out/debug-aarch64
```

You can configure the set of modules that `gen.py` uses with the `--modules`
argument. After running `gen.py` once, you can do incremental builds using
`ninja`.

You can specify an "autorun" script to run at startup with the `--autorun`
parameter. The argument to `--autorun` is the path to a shell script.
This simple example powers off the machine immediately after booting.

```
echo 'dm poweroff' > poweroff.autorun
./packages/gn/gen.py --autorun=poweroff.autorun
./buildtools/ninja -C out/debug-x86-64
```

For a list of all `gen.py` options, run `gen.py --help`.

### Running Fuchsia

The commands above create an `out/debug-{arch}/user.bootfs` file. To run the
system with this filesystem attached in QEMU, pass the path to user.bootfs as
the value of the `-x` parameter in Magenta's start command script, for example:

```
./scripts/run-magenta-x86-64 -x out/debug-x86-64/user.bootfs -m 2048
./scripts/run-magenta-arm64 -x out/debug-aarch64/user.bootfs -m 2048
```

See the [standard build instructions](https://fuchsia.googlesource.com/docs/+/master/getting_started.md#Boot-from-QEMU) for other flags you
can pass to QEMU.

[magenta]: https://fuchsia.googlesource.com/magenta/+/HEAD/docs/getting_started.md "Magenta"
