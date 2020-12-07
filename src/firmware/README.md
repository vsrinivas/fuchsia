# Fuchsia Firmware SDK

The Firmware SDK provides definitions and reference code used to develop
bootloader firmware for Fuchsia devices. The intent is for this SDK to provide
common functionality needed by all firmware, such as the ZBI structure needed
to boot the Zircon kernel, so it doesn't have to be re-implemented from scratch
when bringing up a new board.

## Building the Firmware SDK

The Firmware SDK build rules live in `//sdk/BUILD.gn`. The SDK is not built
in the normal flow, but can be built manually as follows:

1. Add `build_sdk_archives=true` to your gn args, typically by one of:
   1. `fx set ... --args=build_sdk_archives=true`, or
   2. `fx args` and add `build_sdk_archives = true` to the args file
1. Run `fx build sdk:firmware`
1. Find the resulting SDK archive at `<build_dir>/sdk/archive/firmware.tar.gz`

## Porting

Firmware development is usually device-specific so it's not expected that this
code will be usable directly; some porting work will be necessary. However, to
ease the porting burden, everything in the Firmare SDK has a pure C
implementation available, as firmware toolchains are commonly C-only.

General porting steps are:

1. Build the Firmware SDK archive (see instructions above)
1. Unpack the archive in the firmware source tree
1. Add the required files and paths to the firmware build system
1. If necessary, modify SDK headers to work with the firmware source (e.g.
   some files use standard library headers which may not exist)

## Backwards Compatibility

### ABI

The structures used to communicate between the OS and bootloader will maintain
ABI backwards compatibility:

* ZBI format
* vbmeta format
* A/B/R metadata

This backwards compatibility is required so that we can continue to evolve the
Fuchsia implementation as needed without also requiring simultaneous firmware
updates.

### API

The Firmware SDK does not enforce any API compatibility, meaning function
signatures, constant/type names, etc may change at any time. However, any such
changes should be relatively infrequent, and will only affect firmware when
uprevving to a newer Firmware SDK version.

## Components

### ZBI format

The ZBI format defines the structure of ZBI images. The firmware needs to know
this in order to load the ZBI from disk into memory, append a number of ZBI
items, and then pass control to the ZBI kernel.

There's currently no way to provide these definitions alone in an SDK; instead
they are included as part of the larger Zircon sysroot SDK (fxbug.dev/65907).

The ZBI definitions are currently located in the Firmware SDK under
`arch/<arch>/sysroot/include/zircon/boot/`, primarily `image.h`.

### libzbi

libzbi is a reference library, wrapping the ZBI format in a more convenient API.
Firmware is not required to use libzbi, it just has to adhere to the underlying
ZBI format.

libzbi will be in the Firmware SDK under `pkg/zbi/`.

### libabr

libabr provides A/B/R booting support. A/B/R booting is optional, for example a
dev board may have no real need for A/B OTA updates, and it is not mandatory for
booting Zircon.

libabr will be in the Firmware SDK under `pkg/abr/`.

### libavb

libavb provides verified boot support, i.e. using vbmeta images to authenticate
Zircon images loaded from disk before attempting to use them. Like libabr, this
library is not required and may be omitted for things like dev boards.

This library originally comes from
[Android](https://android.googlesource.com/platform/external/avb/). While we
intend to upstream any necessary changes so they should be functionally
identical, firmware should use the copy in the Firmware SDK in case there are
local changes, and to make sure there aren't any version mismatch issues with
the code used by the Fuchsia build to generate the vbmeta images.

libavb will be in the Firmware SDK under `pkg/avb/`.
