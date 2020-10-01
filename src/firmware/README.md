# Firmware Development

The firmware SDK provides definitions and reference code used to develop
firmware for Fuchsia devices.

The eventual goal is to make this SDK contain everything needed for booting
into Fuchsia, so that all the firmware requirements can be met by porting the
SDK code to the device. At the moment though some libraries are not yet
included in the SDK so must be found elsewhere.

## Building the Firmware SDK

The firmware SDK build rules live in `sdk/BUILD.gn`. The SDK is not built
in the normal flow, but can be built manually as follows:

1. Add `build_sdk_archives=true` to your gn args, typically either by one of:

   1. `fx set ... --args=build_sdk_archives=true`
   2. `fx args` and add `build_sdk_archives = true` to the args file

1. Run `fx build sdk:firmware`

1. Find the resulting SDK archive at `<build_dir>/sdk/archive/firmware.tar.gz`

## Porting

Firmware development is usually pretty device-specific so it's not expected
that this code will be usable directly; some porting work will be necessary.
Because of this, there's no API stability requirement - if a device uprevs to
a new firmware SDK, we expect to have to port again including adapting to any
API changes.

However, to ease the porting burden, we do require that everything in the
firmare SDK has a pure C implementation available, as firmware toolchains are
commonly C-only.
