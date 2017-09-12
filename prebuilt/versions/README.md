# Zircon Prebuilt Versions

The `prebuilt/versions` directory mirrors the directory structure found in the
Zircon prebuilt Google Storage bucket at `gs://fuchsia-build/zircon/`.  The
files indicate which versions to download.  The filename to use and where to install
the prebuilts is left up to the tool that does the downloading.

## Example

On Google Storage, this file exists:

```
fuchsia-build/zircon/toolchain/aarch64-elf/Linux-x86_64/d1b546ffcd826482cea63ae67a13d3c98a92bf1e
```

In `prebuilt/versions`, the corresponding file exists:

```
prebuilt/versions/toolchain/aarch64-elf/Linux-x86_64/version.sha

# the file contains the sha of the prebuilt to download:
$ cat prebuilt/versions/toolchain/aarch64-elf/Linux-x86_64/version.sha
d1b546ffcd826482cea63ae67a13d3c98a92bf1e
```
