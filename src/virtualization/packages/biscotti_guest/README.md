# Biscotti Guest

The `biscotti_guest` is a guest system based off the
[Termina VM](https://chromium.googlesource.com/chromiumos/docs/+/HEAD/containers_and_vms.md).

## Building the Kernel

This will build a kernel and deposit the image at
`//prebuilt/virtualization/packages/biscotti_guest/images/x64/Image`.

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
 (host) $ fx set core.x64 
                     --with-base //src/virtualization,//src/virtualization/packages/biscotti_guest
 (host) $ fx build
```

## Boot to Termina

For basic things, booting to the Termina VM is probably the simplest solution.
This provides a minimal linux environment that is read-only, but is faster and
simpler to boot:

```
 (fuchsia) $ guest launch biscotti_guest
 (guest) #
```

## Boot to Debian Container

The Debian container provides a more fully functional linux environment that
allows additional packages to be added via `apt`.

> This is still experimental and is unlikely to work without some tweaks. The
> container networking is hard-coded to use a fixed address that will likely
> need to be changed to match your specific network configuration. It also uses
> the host net device directly which means you'll want to enter the following
> commands directly into the Fuchsia terminal (`fx shell` is unlikely to work).

In one shell, start the guest. This will continue to show logging from the
guest but will not be interactive.
```
  (fuchsia[1]) $ run biscotti
               [INFO:guest.cc(71)] Creating Guest Environment...
               [INFO:guest.cc(138)] Starting GRPC server...
               .... Lots more logging ....
               [INFO:guest.cc(312)] Starting Container...
               [INFO:guest.cc(330)] Container started
               [INFO:guest.cc(312)] Creating user 'machina'...
               [INFO:guest.cc(312)] User created.
               [INFO:guest.cc(312)] Launching container shell...
```

To interact with the container, connect to the serial port:
```
  (fuchsia[2]) $ guest list
               env:0              biscotti
                guest:3           biscotti_guest
  (fuchsia[2]) $ guest serial 0 3
  (container)  machina@stretch:~$ sudo apt-get update
```
