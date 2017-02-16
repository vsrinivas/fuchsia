# Building the toolchain

To build Fuchsia's toolchain, you can use the `scripts/build-toolchain.sh`
script as follows:

```
./scripts/build-toolchain.sh
```

After the build finishes, you can find the built toolchain in `out/toolchain`.

Please note that build can take significant amount of time, especially on
slower machines.

When the sources get updated, you can rebuild the toolchain using the following
commands:

```sh
# update
jiri update

# cleanup the artifacts from the previous build and rebuild the toolchain
./scripts/build-toolchain.sh -c
```
