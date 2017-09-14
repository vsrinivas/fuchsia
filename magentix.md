# Magentix

Magentix is a build configuration of the system that contains the Zircon
kernel, drivers, benchmarks, and some traditional POSIX-like userland tools.
The Magentix configuration can be built with a subset of the full source tree.

## Getting the source

Install the prerequisites described in [getting_source.md], including ensuring
that you have Go 1.6 or newer and Git installed on your PATH.

To create a new Magentix checkout in a directory called `magentix` run the
following commands. The `magentix` directory should not exist before running
these steps.

```
curl -s https://raw.githubusercontent.com/fuchsia-mirror/jiri/master/scripts/bootstrap_jiri | bash -s magentix
cd magentix
export PATH=`pwd`/.jiri_root/bin:$PATH
jiri import magentix https://fuchsia.googlesource.com/manifest
jiri update
```

## Building and running

To build and run Magentix, follow the directions in the [build_system.md]
documentation. The one change is to pass the `-m magentix` argument to
`gen.py`:

```
./packages/gn/gen.py -m magentix
```

This argument tells `gen.py` to generate a build system that builds only the
binaries needed for Magentix.

If you have a fully Fuchsia source tree, you can also build Magentix by passing
`-m magentix` to `gen.py` in the same way.
