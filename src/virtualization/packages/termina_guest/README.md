# Termina Guest

See `src/virtualization/README.md` for more general information.

## Building Linux kernel

Repeat each of the following steps for ARCH=x64 and ARCH=arm64.

Run the script to fetch the remote branch `machina-4.18` and build Linux:
```
$ ./src/virtualization/packages/termina_guest/make_linux_kernel.sh \
  -l /tmp/linux/source \
  -o prebuilt/virtualization/packages/linux_guest/images/${ARCH}/Image \
  -b machina-4.18 \
  ${ARCH}
```

Note: `-b` specifies the branch of `zircon_guest` to use. You can modify
this value if you need a different version or omit it to use a local
version.

## Updating CIPD with a more recent Linux kernel

The script `update_cipd_kernel_package.sh` will download the latest revision of
the Linux kernel from the `zircon_guest` repository, build it using the
`make_linux_kernel.sh` script above, and then upload it to CIPD.

The file `integration/fuchsia/prebuilts` should be updated to point to point to
the new version using the instance ID of the package you created. The instance
ID is printed in the output of the script above, but if you missed it, you
can find the instance ID with CIPD like so:

```
$ fx cipd search \
  fuchsia_internal/linux/linux_kernel-<branch>-${ARCH} \
  -tag "git_revision:<git revision>" \
```
