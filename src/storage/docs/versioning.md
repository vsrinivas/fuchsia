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

As an example for special handling of different revisions of backwards-compatible formats, say there
is a minor bug that was corrected that made some previously valid data layout no longer permitted:

  * Old code should still be allowed to use the device since it can still read all of the data
    and can still write a format understood by newer revisions.

  * New code would like to correct the no-longer-valid format to a newer format when it occurs and
    it should be able to tell when this migration is required.

  * Utilities such as `fsck` need to know exactly what format to expect. If it sees the invalid
    structure in a device written only by the newer version of the code, it knows there is a serious
    error. But if the device was written to by older code, it knows that this is expected and to
    continue.

## Concepts

  * **Format version:** The version of the format on device. Different format versions are not
    compatible. Any non-backwards-compatible changes should increment the format version.

  * **Revision:** Any change to how data is stored in any way should update the revision. It may or
    may not be backwards-compatible.

  * **Oldest revision:** The oldest revision of the software (compatible with the format version)
    that has written to the device.

We do not maintain minor version numbers. The "revision" covers most of the uses of a minor version
number in other systems (differentiating different but compatible formats). But because a device may
be potentially written to by a range of software revisions (as long as they all understand the
format version), there is no single "revision number" of the data.

## Requirements

Persistent storage systems should maintain two numbers in the header of their data:

  * Format version.
  * Oldest revision.

Systems encode data in a way that allows formats to be added without invalidating older versions
when possible. For example, if compression is supported in a filesystem, the compression algorithm
should be stored on the file. This allows adding additional compression algorithms without
invalidating older data.

To support future updates, all reserved regions in metadata should be zero initialized at format
time. Unit tests should verify this. Verification tools should be lenient about checking reserved
sections i.e. they should *not* check that reserved sections are zeroed. Similarly, length fields
that allow structures to expand should be loosely checked. If it makes sense to do so, consider
adding a "strict" option (disabled by default) that performs these checks.

Metrics for the format version, oldest revision, and current revision should be available via
Cobalt.

## Maintaining and using versions

#### Upon formatting a device

When a device is initially created, the current format version and revision should be written to the
header.

#### Upon opening a device

Software that opens a device should first check the format version. If the format version is
larger than expected, the operation should fail and no operations should be attempted with the
device.

Software that opens a device for writing should next check the device's current oldest revision. If
the software revision is less than the oldest revision stored on the device, it should update the
device's oldest revision to the current software revision and continue. Software with newer
revisions should not increase the oldest revision without performing an update.

#### Upon performing minor updates

Sometimes a migration may need to be done. For the example given in the "Background" section, we may
want to check for and correct the newly-invalid format. In this case, the software can check whether
the update is required by looking at the oldest revision of the device. If it is before the revision
with the fix, the software knows that there may be data on the device that has the older format and
should perform an update.

If the persistent data is updated so that none of it can be considered to have been written by an
older revision, the oldest revision value should be update to the current value. This will prevent
performing the migration in the future so long as no older revisions of the software write to the
device.
