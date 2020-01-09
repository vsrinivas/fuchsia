# blackout - power failure testing for the filesystems

TODO(ZX-4721): write a guide for writing additional power failure tests using this framework.

## Running power failure tests

The tests are not built by default. They need to be included in your fx set line -

```
fx set core.x64 --with //tools/blackout:all
fx build
```

The tests require a spare partition to operate on, because they format it (and they can't be
mounted twice anyway). This is a little tricky, and there are several ways to do it. My prefered
approach is here - https://fuchsia-review.googlesource.com/c/fuchsia/+/303249. This approach
makes the build create a spare partition for you whenever it generates a new image.

Set up a fuchsia environment in the standard way. for example (in separate shells) -

```
# shell 1
fx emu -N
```
```
# shell 2
fx serve -v
```
```
# shell 3 (optional)
fx syslog
```
```
# shell 4 (optional)
fx shell
```

Next we collect a bit of information we need to run the test. Use `lsblk` on the device to find
the block ID for the spare partition you created (it's probably 003 if you used the above
approach). If you are doing this on real hardware, have a relay, and would like to do a hard
reboot as part of the test, find the path to the relay device on the host machine.

Run the test with the following command on the host machine -

```
./out/default/host_x64/<test_name_with_underscores>_host "/dev/class/block/<block-id>" "$(fx get-device-addr)"
```

If you have a power relay to cut power to real hardware, add `--relay /dev/<relay-device>`.
