# Fuchsia Self-Hosted Build
## Build Fuchsia Native Tools
* If not already cloned, check out the `gcc_none_toolchains` repository.
```bash
git clone https://fuchsia.googlesource.com/third_party/gcc_none_toolchains
```
* Build the Fuchsia-native tools.
```bash
cd <gcc_none_toolchains-dir>
./do-build \
	--host x86_64-fuchsia \
	--target x86_64-fuchsia \
	--sysroot <path-to-target-sysroot> \
	--strip
```
By default, this will leave the compiler in the
`x86_64-fuchsia-<gcc-ver>-native` subdirectory of the current directory.

## Build Fuchsia-Hosted Bare-Metal Tools
* In the same `gcc_none_toolchains` repository, build the fuchsia-hosted
bare-metal tools:
```bash
./do-build \
	--host x86_64-fuchsia \
	--target x86_64-none \
	--sysroot <path-to-target-sysroot> \
	--strip
```
By default, this will leave the compiler in the
`x86_64-elf-<gcc-ver>-Fuchsia-x86_64` subdirectory of the current directory.

## Build GNU make
* Checkout make sources, if needed:
```bash
git clone https://fuchsia.googlesource.com/third_party/make
```

* Follow the instructions provided in README.md of the GNU make sources
to build a Fuchsia-hosted version of make.

## Build utilities (Shouldn't be necessary)
The following utilities are required by the build scripts. As long as your
build manifest included the sbase project, these should have been installed
with your Fuchsia build:
```text
uname
which
tr
find
mv
cmp
sort
basename
sed
```

## Build Zircon
Follow standard Zircon/Fuchsia source configuration instructions. Note that
you will want a minimum of `zircon` and `sbase`.

In order for gcc-built executables to run in Fuchsia, we will need the gcc
runtime libraries, built for Fuchsia. They were built as part of the Fuchsia
native build, but we need them to be installed into one of the standard runtime
library locations on the target. One way to do this is to add the libraries to
the manifest lines in `zircon/kernel/engine.mk`:
```code
USER_MANIFEST_LINES += lib/libgcc_s.so.1=<path-to-gcc>/x86_64-fuchsia-6.3.0-native/lib/libgcc_s.so.1
USER_MANIFEST_LINES += lib/libgcc_s.so=<path-to-gcc>/x86_64-fuchsia-6.3.0-native/lib/libgcc_s.so
USER_MANIFEST_LINES += lib/libstdc++.so=<path-to-gcc>/x86_64-fuchsia-6.3.0-native/lib/libstdc++.so
```
Follow the standard Zircon/Fuchsia build instructions, and run the resulting
image on the desired target.

## Copy Files Onto Target
Create a new empty directory in the target environment (for this example, we'll
use /data). For your own sanity, this should be persistent storage of some
sort. For this example, we'll use the following /data subdirectories:
```text
bin             Directory for miscellaneous tools not provided in Fuchsia or Zircon
gcc             Native gcc installation
gcc-bare-metal  Fuchsia-hosted bare-metal tools
zircon         The Zircon source files
sysroot         The sysroot of the installed Fuchsia
```
* Netcp all native gcc files into `/data/gcc`
```bash
cd <gcc-install-dir>/x86_64-fuchsia-<gcc-ver>-native
for filename in `find . -type f`
do
  echo "Copying $filename"
  netcp "$filename" ":/data/gcc/$filename"
done
```
* Netcp all bare-metal gcc files into `/data/gcc-bare-metal`
```bash
cd <gcc-install-dir>/x86_64-elf-gcc-<gcc-ver>-Fuchsia-x86_64
for filename in `find . -type f`
do
  echo "Copying $filename"
  netcp "$filename" ":/data/gcc-bare-metal/$filename"
done
```
* Netcp make to `/data/bin`
```bash
netcp <make-build-dir>/make :/data/bin
```
* Copy the zircon source code onto the target, either using netcp or a mounted
image, or git. Note that if you are using the same source tree as was used to
build the target image, you'll want to remove any modifications to
kernel/engine.mk that were made for previous steps (or update them to reflect
the target location of these files), otherwise the build will fail when trying
to generate the bootfs image.
```bash
cd <path-to-zircon>
for filename in `find . -name .git -prune \
                        -o -path "./build-*" -prune \
                        -o -path "./prebuilt*" \
                        -o -type f -print`
do
  echo "Copying $filename" 
  netcp $filename :/data/zircon/$filename
done
```
* Copy the Zircon sysroot onto the target device in `/data/sysroot`
```bash
cd <zircon-sysroot>
for filename in `find . -type f`
do
  echo "Copying $filename"
  netcp $filename :/data/sysroot/$filename
done
```
* Copy the compiler shared libraries into `/data/sysroot/lib`
```bash
cd <path-to-gcc>/x86_64-fuchsia-<gcc-ver>-native
for filename in libstdc++.so libgcc_s.so libgcc_s.so.1
do
  echo "Copying $filename"
  netcp lib/$filename :/data/sysroot/lib/$filename
done
```

## Build Zircon
On the Fuchsia target, add `/data/bin`, `/data/gcc/bin`, and `data/gcc-bare-metal/bin` to your `PATH`
```bash
export PATH="$PATH:/data/bin:/data/gcc/bin:/data/gcc-bare-metal/bin"
```
* Finally, start the build in the zircon directory:
```bash
cd zircon
make \
    HOST_SYSROOT=/data/sysroot \
    ARCH_x86_64_TOOLCHAIN_PREFIX="x86_64-elf-" \
    HOST_TOOLCHAIN_PREFIX=""
```
