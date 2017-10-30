Rust DDK Toy Installation Instructions
======================================
In order to have the system load this driver at boot, perform the following steps:

1. Run 'fargo build' in this directory.
2. SCP the built .so file to the /system/driver/ folder for whatever you're targeting. This will likely
   require a persistent disk image. Ex. command:
      scp -F $FUCHSIA_ROOT/out/release-x86-64/ssh-keys/ssh_config target/target/x86_64-unknown-fuchsia/debug/libddk_toy.so $DEST_DEVICE:/system/driver/
3. Reboot the device.
