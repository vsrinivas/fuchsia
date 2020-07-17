# Firmware libzbi

This library is meant to be a reference implementation for how firmware can
work with ZBI images. The general flow is to load the ZBI image from disk,
append some additional items to it at runtime, and then pass control to the
kernel image contained within.

Using this library is optional; it's just a convenience wrapper around the ZBI
definitions, and firmware is free to write their own wrappers or modify this
library as needed. The important thing is adhering to the ZBI ABI as defined in
the Zircon sysroot, in particular `boot/image.h`.

This library is meant specifically for out-of-tree firmware code. Fuchsia code
should instead use the in-tree ZBI utilities which are more powerful but have
additional dependencies unsuitable for firmware.

## Compatibility

In order to be compatible with a wide variety of bootloader toolchains, this
library only requires:

 * C99 compiler
 * Zircon sysroot ZBI definitions
 * Fixed-size integer types (`uint8_t`, `int32_t`, etc)
 * Basic memory manipulation functions (`memcmp()`, `memcpy()`)

There are no guarantees on the API stability of this library. It's expected that
all firmware will have some minimal porting work to do when importing or
uprevving this library, which may include adapting to API changes.

## Porting

1. Unpack the firmware SDK into your source tree.

1. Add source files in `pkg/libzbi/` to the build.

1. Find additional header dependencies in `arch/<arch>/sysroot/include`. This
   directory can be added to the include search path directly, or you can copy
   the required headers out and update the libzbi `#include` paths.
