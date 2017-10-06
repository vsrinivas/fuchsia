# Fuchsia Build Notes

These are notes on using the `gen.py` script and `ninja` directly for builds,
and the `scripts/run-zircon-*` scripts to launch QEMU.

You can alternatively use the [standard build instructions](https://fuchsia.googlesource.com/docs/+/master/getting_started.md#Setup-Build-Environment)
using commands defined in `env.sh` instead.

### Build Zircon and the sysroot

First, you need to
[build the Zircon kernel](https://fuchsia.googlesource.com/zircon/+/master/docs/getting_started.md)
and the sysroot:

```
(cd zircon; make -j32 zircon-pc-x86-64)
./scripts/build-zircon.sh
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

You can configure the set of packages that `gen.py` uses with the `--packages`
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
the value of the `-x` parameter in Zircon's start command script, for example:

```
./scripts/run-zircon-x86-64 -x out/debug-x86-64/user.bootfs -m 2048
./scripts/run-zircon-arm64 -x out/debug-aarch64/user.bootfs -m 2048
```

See the [standard build instructions](https://fuchsia.googlesource.com/docs/+/master/getting_started.md#Boot-from-QEMU) for other flags you
can pass to QEMU.

[zircon]: https://fuchsia.googlesource.com/zircon/+/HEAD/docs/getting_started.md "Zircon"


### Running on ARM Hardware

To build a fuchsia image for a particular ARM hardware target, you must also
provide the zircon project name for that particular target to gen.py via the
zircon_project argument. For example, to build a fuchsia image for the
Raspberry Pi 3, you might invoke gen.py as follows:

```
./packages/gn/gen.py --zircon_project=zircon-rpi3-arm64 --target_cpu=aarch64
./buildtools/ninja -C out/debug-aarch64
```
