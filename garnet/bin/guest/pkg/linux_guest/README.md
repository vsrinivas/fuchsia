# Linux Guest

See `garnet/bin/guest/README.md` for more general information.

## Updating Linux image

Repeat each of the following steps for ARCH=x64 and ARCH=arm64.

Run the script to build Linux:
```
$ ./garnet/bin/guest/pkg/linux_guest/mklinux.sh \
  -l /tmp/linux/source \
  -o garnet/bin/guest/pkg/linux_guest/images/${ARCH}/Image \
  -b machina-4.18 \
  ${ARCH}
```

Note: `-b` specifies the branch of zircon_guest to use. You can modify this
value if you need a different version or omit it to use a local version.

Build the sysroot:
```
$ ./garnet/bin/guest/pkg/linux_guest/mksysroot.sh \
  -r \
  -u \
  -p garnet/bin/guest/pkg/linux_guest/images/${ARCH}/disk.img \
  -d /tmp/toybox \
  -s /tmp/dash \
  S{ARCH}
```

Build the tests image:
```
$ ./garnet/bin/guest/pkg/linux_guest/mktests.sh \
  -u \
  -o garnet/bin/guest/pkg/linux_guest/images/${ARCH}/tests.img \
  -d /tmp/linux-tests \
  S{ARCH}
```

Ensure that `linux_guest` is working correctly. Then upload the images to cipd. There is a cipd binary at `//buildtools/cipd` and `cipd auth-login` must be run before any of the following commands.

Use the git revision hash from `zircon-guest.googlesource.com/third_party/
linux` as the `kernel_git_revision` tag and from `zircon-guest.googlesource.com/linux-tests` as the `tests_git_revision` tag.

```
$ cipd create \
  -in garnet/bin/guest/pkg/linux_guest/images/${ARCH} \
  -name fuchsia_internal/linux/linux_guest-<version>-${ARCH} \
  -install-mode copy \
  -tag "kernel_git_revision:<git revision>" \
  -tag "tests_git_revision:<git revision>"
```

Then update `garnet/tools/cipd_internal.ensure` to point to the new version using the instance ID of the package you created. You can find the instance ID with CIPD like so:
```
$ cipd describe \
  fuchsia_internal/linux/linux_guest-<version>-${ARCH} \
  -version "kernel_git_revision:<git revision>" \
  -version "tests_git_revision:<git revision>"
```

## Updating the Linux kernel version

Create a branch within `zircon-guest.googlesource.com/third_party/linux` with
naming scheme `machina-X.XX` where `X.XX` is the kernel version. Make sure to
import all the machina defconfig files from the latest branch. Make sure
`linux_guest` works correctly before updating the images as above. Please also
update the instructions above, and in bin/guest/README.md, to use the most
recent branch.
