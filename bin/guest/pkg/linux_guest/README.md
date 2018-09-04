# Linux Guest

See `garnet/bin/guest/README.md` for more general information.

## Updating Linux image

Repeat each of the following steps for ARCH=x64 and ARCH=arm64.

Run the script to build Linux:
```
$ ./garnet/bin/guest/pkg/linux_guest/mklinux.sh -l /tmp/linux/source -o garnet/bin/guest/pkg/linux_guest/images/${ARCH}/Image ${ARCH}
```
Note: You can pass `-b LINUX_BRANCH` to the script if you need to specify a
branch other than `machina`.

Repeat for the sysroot:
```
$ ./garnet/bin/guest/pkg/linux_guest/mksysroot.sh -r -p garnet/bin/guest/pkg/linux_guest/images/${ARCH}/disk.img -d /tmp/toybox -s /tmp/dash S{ARCH}
```

Ensure that `linux_guest` is working correctly. Then upload the images to cipd.
Use the git revision hash from `zircon-guest.googlesource.com/third_party/
linux` as a tag.
```
cipd create -in garnet/bin/guest/pkg/linux_guest/images/${ARCH} -name fuchsia_internal/linux/linux_guest-<version>-${ARCH}-install-mode copy -tag "git_revision:<git revision>"

```

Then update `garnet/tools/cipd_internal.ensure` to point to the new version
(with the appropriate git hash).

## Updating the Linux kernel version

Create a branch within `zircon-guest.googlesource.com/third_party/linux` with
naming scheme `machina-X.XX` where `X.XX` is the kernel version. Make sure to
import all the machina defconfig files from the latest branch. Make sure
`linux_guest` works correctly before changing the reference of the `machina`
branch to point to the new branch, and updating the images as above.
