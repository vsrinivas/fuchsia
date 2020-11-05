# Storage versioning

## Current status

This document describes the practices adopted by the local storage team but implementing these rules
has not yet been completed for all storage formats.

## Background and requirements

Our on-disk [storage systems](/docs/concepts/filesystems/filesystems.md) need to persist across
updates and multiple versions of the system may need to interact with this data. There will be some
changes that will not be be backwards-compatible and software must avoid changing data it can not
full understand. There can also be minor updates to the storage format that are backwards-compatible
but may need special handling.

As an example for special handling of different versions of backwards-compatible formats, say there
is a minor bug that was corrected that made some previously valid data layout no longer permitted:

  * Old code should still be allowed to use the device since it can still read all of the data
    and can still write a format understood by newer versions.

  * New code would like to correct the no-longer-valid format to a newer format when it occurs and
    it should be able to tell when this migration is required.

  * Utilities such as `fsck` need to know exactly what format to expect. If it sees the invalid
    structure in a device written only by the newer version of the code, it knows there is a serious
    error. But if the device was written to by older code, it knows that this is expected and to
    continue.

## Concepts

  * **Major version:** The non-backwards-compatible version of the format on device. Different
    major versions are not compatible. Any non-backwards-compatible changes should increment the
    major version.

  * **Minor version:** Backwards-compatible changes to how data is stored on the disk should update
    the minor version.

  * **Oldest minor version:** The oldest minor version of the software that has written to the
    device.

## Requirements

Persistent storage systems should maintain two numbers in the header of their data:

  * Major version.
  * _Oldest_ minor version.

Systems should encode data in a way that allows formats to be added without invalidating older
versions when possible. For example, if compression is supported in a filesystem, the compression
algorithm should be stored on the file. This allows adding additional compression algorithms without
invalidating older data.

To support future updates, there should be some reserved bytes in the metadata if possible, and all
reserved regions in metadata should be zero initialized at format time. Unit tests should verify
this. Verification tools should be lenient about checking reserved sections i.e. they should *not*
check that reserved sections are zeroed. Similarly, length fields that allow structures to expand
should be loosely checked. If it makes sense to do so, consider adding a "strict" option (disabled
by default) that performs these checks.

Metrics for the major version and oldest minor version should be available via Cobalt.

## Maintaining and using versions

#### Upon formatting a device

When a device is initially created, the current on-disk format's major and minor version should be
written to the header.

#### Upon opening a device

Software that opens a device should first check the major version. If the major version is
larger than expected, the operation should fail and no operations should be attempted with the
device.

Software that opens a device for writing should next check the device's current oldest minor
version. If the software minor version is less than the oldest minor version stored on the device,
it should update the device's oldest minor version to the current software minor version and
continue. Software with newer minor versions should not increase the oldest minor version without
performing an update.

#### Upon performing minor updates

Sometimes a migration may need to be done. For the example given in the "Background" section, we may
want to check for and correct the newly-invalid format. In this case, the software can check whether
the update is required by looking at the oldest minor version of the device. If it is before the
minor version with the fix, the software knows that there may be data on the device that has the
older format and should perform an update.

If the persistent data is updated so that none of it can be considered to have been written by an
older minor version, the oldest minor version value should be update to the current value. This will
prevent performing the migration in the future so long as no older minor versions of the software
write to the device.

## Why keep only the oldest minor version?

Why do we keep the oldest minor version rather than the newest one that has written to the disk?
Most systems use only the newest version.

We expect increments to the (backwards-compatible) minor version to be of two general types:
additions of new features that older versions of the software can ignore, and increasing the
strictness of requirements of the format.

In both of these cases, if an older version of the software writes to the disk, assumptions about
the data added in a newer version may become invalid. For example, added tracking information may
get out-of-sync because the older version doesn't know how to update it, or the older version may
re-introduce subsequently disallowed patterns. If version 3.0 writes to a version 3.1 disk, it's
not necessarily version 3.1 any more and we don't want to treat it as such. In these cases, the
newer versions of the sofware will want to perform upgrade-specific checks or migrations.

How can a newer version of the software know if something it made exists if the disk has only the
oldest version? Say a new data format was added or a new table was added to the metadata, how does
the newer version know if the device has these structures? For backwards-compatible additions, it
should just check for the presence of the structures which should always be additive. New data
should be added in 0-initialized reserved regions so this is always possible. As mentioned earlier,
checks on these reserved areas should be lenient. For more subtle changes, bits can be added to the
reserved region that indicates a change happened or something might be present.
