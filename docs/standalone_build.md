Garnet Standalone Build
=======================

To get the source code for the Garnet layer, using the following commands (see [Getting Source](https://fuchsia.googlesource.com/docs/+/master/getting_source.md) for more
information):

```
curl -s https://raw.githubusercontent.com/fuchsia-mirror/jiri/master/scripts/bootstrap_jiri | bash -s garnet
cd garnet
export PATH=`pwd`/.jiri_root/bin:$PATH
jiri import garnet https://fuchsia.googlesource.com/manifest
jiri update
```

To build the default set of packages for the Garnet layer for x86-64, use the following
commands:

```
scripts/build-zircon.sh -t x86_64
packages/gn/gen.py --debug -m garnet/packages/default
buildtools/ninja -C out/debug-x86-64
```

For 64bit ARM (aarch64):

```
scripts/build-zircon.sh -t aarch64
packages/gn/gen.py -t aarch64 --debug -m garnet/packages/default
buildtools/ninja -C out/debug-aarch64
```

If you're using `env.sh`, you can specify `garnet/packages/default` using
`fset` (see [Getting Started](https://fuchsia.googlesource.com/docs/+/master/getting_started.md#setup-build-environment) for more information).