# blackout - power failure testing for the filesystems

TODO(fxbug.dev/34494): write a guide for writing additional power failure tests using this framework.

## Running power failure tests

The tests are not built by default. They need to be included in your fx set line -

```
fx set core.x64 --with //tools/blackout:all
fx build
```

If you are using real hardware, you will likely want to be using a netbooted environment, in which case you will add the `--netboot` flag to your `fx set` line.

```
fx set core.x64 --with //tools/blackout:all --netboot
```

The tests require a spare partition to operate on, because they format it (and partitions can't
be mounted twice anyway). There are two ways to achieve this, depending on the device being used
to test.

### qemu

Create a local file to be used as the test partition.

```
truncate --size=300M blackout.bin
```

Run the virtual machine with the new block device attached. The additional options disable the
qemu writeback cache that is normally automatically used for attached block devices, because it
can hide errors.

```
# shell 1
fx qemu -Nk -m 16384 -- -drive file=$PWD/blackout.bin,index=0,media=disk,cache=directsync
```

Set up the rest of the environment in the standard way. For example (in separate shells) -

```
# shell 2
fx serve
```
```
# shell 3 (optional)
fx log
```
```
# shell 4 (optional)
fx shell
```

### real hardware

Make sure you have `--netboot` on your fx set line. Set up your test device with the bootloader
and zedboot, but don't pave it. When your device is booted into zedboot, run the following
commands -

```
# shell 1
fx netboot
```
```
# shell 2
fx serve-updates
```

As of January 2020, the `fx netboot` script quits after one serve. This isn't desirable when
doing many test runs in a row. A quick edit in the `//tools/devshell/netboot` script, removing
the `-1` from the call on the last line, will stop this behavior.

### running the test

Next we collect a bit of information we need to run the test. Use `lsblk` on the device to find
the block ID for the spare partition you created. If you are doing this on real hardware, have a
relay, and would like to do a hard reboot as part of the test, find the path to the relay device
on the host machine.

Run the test with the following command on the host machine -

```
./out/default/host_x64/<test_name_with_underscores>_host "/block/device" "$(fx get-device-addr)"
```

If you have a power relay to cut power to real hardware, add `--relay /dev/<relay-device>`.

This will run the test once. Blackout tests also have the ability to run tests multiple times in
a row by adding the `-i <iterations>` flag. This will run the test `iterations` times,
aggregating statistics on the test runs, particularly the number of failures. The `-f` flag can
also be added when `-i` is provided, which will cause blackout to exit on the first verification
failure it encounters. This is useful for manual investigation of failures - blackout won't
reformat the drive again, leaving it in it's corrputed state.
