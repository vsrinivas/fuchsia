# Debian Guest

The `debian_guest` package provides a more substantial Linux environment than
that provided by the `linux_guest` package.

## Build with Bundled Root Filesystem

These steps will walk through building a the package with the root filesystem
bundled as a package resource. The root filesystem will appear writable but
all writes are volatile and will disappear when the guest shuts down.

```
$ cd $FUCHSIA_DIR
$ ./garnet/bin/guest/pkg/debian_guest/build-image.sh x64
$ fx set x64 --packages "garnet/packages/experimental/debian_guest"
$ fx full-build
$ fx boot
```

To boot on a VIM2, replace `x64` with `arm64`.

## Build with a Persistent Disk

To achieve a persistent disk, we'll build the root filesystem on a USB stick
that will be provided to the guest.

```
$ cd $FUCHSIA_DIR

# Insert the USB drive into your workstation. The build-usb.sh script will ask
# you for the disk name. Once the script completes, insert the USB stick into
# the target device.

$ ./garnet/bin/guest/pkg/debian_guest/build-usb.sh x64
$ fx set x64 --packages "garnet/packages/experimental/debian_guest" --args "debian_guest_usb_root=true"
$ fx full-build
$ fx boot
```

To boot on a VIM2, replace `x64` with `arm64`.

## Running `debian_guest`

Once booted:

```
guest launch debian_guest
```

## Telnet shell

The Debian system exposes a simple telnet interface over vsock port 23. You can
use the `guest` CLI to connect to this socket to open a shell. First we need to
identify the environment ID and the guest context ID (CID) to use:

```
$ guest list
env:0             debian_guest
 guest:3          debian_guest
```

The above indicates the debian guest is CID 3 in environment 0. Open a shell
with:

```
$ guest socat 0 3 23
```
