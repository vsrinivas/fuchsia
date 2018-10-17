# Biscotti Guest

The `biscotti_guest` is a guest system based off the
[Termina VM](https://chromium.googlesource.com/chromiumos/docs/+/master/containers_and_vms.md).

## Building the Kernel

This will build a kernel and deposit the image at
`//garnet/bin/guest/pkg/biscotti_guest/images/x64/Image`.

```
 (host) $ cd $BISCOTTI_GUEST_DIR
 (host) $ ./mklinux.sh x64
```

## Building the Termina Disk Image

The Termina disk image is built out of the ChromiumOS source tree. Start by
following the ChromiumOS setup steps
[here](https://sites.google.com/a/chromium.org/dev/chromium-os/quick-start-guide).

Once you have the necessary dependencies installed and the source checked out you
can proceed:

```
 (host) $ cd CROS_SRC_DIR
 (host) $ cros_sdk
 (host) $ export BOARD=tatl
 (host) $ cros_sdk -- ./build_packages --board=${BOARD} --nowithautotest
 (host) $ cros_sdk -- ./build_image --board=${BOARD} test
 (host) $ cros_sdk -- ./termina_build_image --image ../build/images/tatl/latest/chromiumos_test_image.bin -t --output /home/$USER/tatl
 (host) $ cp ./chroot/home/$USER/tatl/vm_rootfs.img  $BISCOTTI_GUEST_DIR/images/x64/disk.img
```

## Build Fuchsia

```
 (host) $ cd $FUCHSIA_DIR
 (host) $ fx set x64 --products "garnet/products/default" \
                     --packages "garnet/packages/experimental/disabled/biscotti_guest"
 (host) $ fx full-build
```

Boot the image on your device.

```
 (fuchsia) $ guest launch biscotti_guest
 (guest) #
```
