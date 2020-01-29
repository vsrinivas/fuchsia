# libabr

## Introduction

This library implements the bootloader A/B/R slot logic expected by Fuchsia.
This logic is not required to boot Zircon or run a basic Fuchsia system, but it
is required to enable Fuchsia's over-the-air (OTA) update features.

The term 'slot' is used to refer to a set of partitions that update together. If
an update fails, the partitions fall back together. This works because there are
two copies of each partition on disk. By convention, the slots used for normal
updates are denoted A and B, and the corresponding partitions are labeled with a
suffix of "\_a" or "\_b". The slot used for recovery is denoted R; this is
chosed when neither A nor B is bootable. This library also uses slot index
values which correspond with these slots:
 - Slot A: 0
 - Slot B: 1
 - Slot R: 2

## Choose Early

It is best to use an A/B update scheme for both firmware and OS images.
Otherwise a system can get into a mismatched state where newer firmware
interacts with an older OS or vice versa. Updating everything together keeps
deployment and compatibility simple. The A/B decision can be made as soon as
metadata storage can be read and to make this easier this library can be used in
read-only mode so an A/B decision can be made even if metadata is read-only.
This library is also designed with minimal dependencies, small code size, and
low memory usage in order to ease integration into early boot environments.

## Usage Example

The following code is an example of how to use libabr to make an A/B decision
during boot when metadata is read-only.

```c
#include "libabr.h"
...
AbrOps ops = {
  .context = NULL,
  .read_abr_metadata = MyReadAbrMetadata,
  .write_abr_metadata = NULL
};
AbrSlotIndex slot_index = AbrGetBootSlot(
    &ops,
    false, /* update_metadata */
    NULL   /* is_slot_marked_successful */);
...
```

> Note: libabr.h is the only header you need to include.

## Metadata

Libabr uses 32 bytes of persistent metadata to maintain state information. The
related structures are defined in abr_data.h. This data is mutable and may be
modified by the bootloader or the OS during normal operation. The metadata is
the only input to the A/B/R boot decision; there are no other arguments,
configuration, or environment data that affect the A/B/R decision. In other
words, given the 32 bytes of metadata, the A/B/R decision is deterministic. When
multiple libabr instances are used during boot as part of different bootloader
or firmware images, they will all compute the same A/B/R decision given the same
metadata. As a result, only one bootloader should be responsible for updating
the metadata during boot. Usually this is the *main* bootloader which is the
last to run before handing off to an operating system.

Metadata tracks, for slots A and B:

-   Whether a slot is *bootable*, that is, whether it is expected to boot
    successfully.
-   Whether a slot has booted successfully since the last update.
-   A relative priority used to choose between multiple bootable slots. When a
    slot is marked as *active* the metadata is set to reflect the slot is
    *bootable* and has the highest priority.
-   An attempt counter which is only relevant when a slot is marked as
    *bootable* but not *successful*.

### Updating Metadata

The most difficult and nuanced part of an A/B update is deciding when to mark a
boot as successful. This **should not** be done by the bootloader except in
response to an operator command. Similarly, the bootloader should not normally
set a slot as *active* except in response to an operator command. These kinds of
metadata changes are typically carried out by a high level update system.

A bootloader may mark a slot as *unbootable* in the event of an unrecoverable
boot error, but libabr will never mark this unless `AbrMarkSlotUnbootable()`
is called explicitly.

Libabr *will* update metadata if `AbrGetBootSlot()` is called with
`update_metadata` set to true. However, only the boot attempt counter will be
updated and only when the slot has not been marked *successful*.

## Recovery

Note that `AbrGetBootSlot()` never fails. If something goes wrong while
attempting to read metadata, the logic will choose slot R. If something goes
wrong while attempting to update metadata, the error is ignored.

The difficult part is responding to an unexpected slot R decision. The *main*
bootloader which will boot the OS is expected to invoke Fuchsia recovery.
Earlier in boot, however, the ideal response is not so clear. The following
responses are acceptable:

-   Enter a recovery mode specific to the hardware or firmware.
-   Pick a default, say slot A, and attempt to boot. This is only acceptable if
    failure to boot the default slot will result in another acceptable response
    such as attempting the other slot, or entering a hardware-specific recovery
    mode.

The most important thing is that the recovery response should not result in
unrecoverable hardware. This may seem obvious, but it can happen. For example,
it is usually not desirable if the response results in a boot loop, or in a
system halt.

## Porting and Integration

The library code itself is designed to be portable with minimal dependencies. It
should work with any modern C toolchain. There are a few dependencies in
`abr_sysdeps.h` that need to be implemented, but these should be fairly
straightforward in most environments and a libc-dependent implementation is
provided.

### Implementing Ops

There are I/O operations that need to be implemented for managing metadata. How
and where metadata is stored is implementation-specific. If metadata is
read-only in the context of an implementation, `write_abr_metadata` can
be set to NULL. See abr_ops.h for details.

### Testing

The unit tests provided here are designed to run on the host, which is unlikely
to match the architecture and constraints of the target implementation. It is
recommended to also run the unit tests in the target environment if possible. To
run the host tests in the fuchsia dev environment, do something like:
```
$ fx set core.x64 --with //src/firmware:host_tests
$ fx run-host-tests libabr_unittests
```
