# bootserver

**bootserver** is a tool that takes a set of images and paves a device. The go
bootserver is meant to deprecate the C version at
[//tools/bootserver_old](https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/bootserver_old)
and will be backwards compatible with it.

The bootserver executable is not actually used by the infrastructure, but the
library is used by
[botanist](https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/botanist)
(which is used by the infrastructure) to pave devices. The executable is built
and uploaded through
[artifactory](https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/artifactory)
to GCS along with other build artifacts to allow developers/testers to download
it and pave their local devices with the images produced by a particular build.

## Pave with explicitly specified images

The bootserver tool can take in specific images as command line arguments to
pave either zedboot or fuchsia. It uses the same arguments as bootserver_old.
See the flags at
[cmd/main.go](https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/bootserver/cmd/main.go)
to see which flags correspond to which images and also which unsupported
bootserver_old flags are remaining.

## Pave with image manifest

Alternatively, you can pave using the `-images` flag with the images.json
manifest produced by a build. The manifest should follow the schema at
[//tools/build/images.go](https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/build/images.go)
and include all the necessary images for paving. This will automatically be
generated after a `gn gen` or `fx set` command in the build out directory, but
the images will actually have to be built with `ninja` or `fx build` before you
can call bootserver with it.

The `-images` flag takes either a path to a local image manifest on the
filesystem, or it can also take a GCS path to an image manifest for a particular
build (i.e. gs://fuchsia-artifacts/builds/\<build id\>/images/images.json). If
using a GCS path, the images will be downloaded from the same directory in GCS
as the manifest.

The `-images` flag must be used in conjunction with the `-mode` flag. The way
bootserver determines which files to pave the device with is by looking
at the `bootserver_pave`/`bootserver_pave_zedboot`/`bootserver_netboot` fields of
each image entry in the manifest, and depending on the `mode` provided (either
`pave`, `pave-zedboot`, or `netboot`), it chooses the images with non-empty args
for that mode to send to the device.
