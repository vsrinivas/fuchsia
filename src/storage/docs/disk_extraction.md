# Disk extraction

This document describes ways to retrieve a disk image from a target that is
running fuchsia.

## Background

Run time state of storage software is split between in-memory and on-disk
structures. Crash or core dumps of the affected process allows you to inspect
in-memory state and dumping disk image helps you debug on-disk state.

Accessing a disk is easier if the developer has full control of the
environment (physical device or a VM) in which the issue is seen and has the
ability to log into the system to run debug commands.

If you do not have full control over the environment, the disk extractor
library and tool only retrieves information such as the filesystem metadata of
the storage without including personally identifiable (PII). By only extracting
relevant portions of the storage, the size of the extracted image remains small.

## Current state

At the moment only minfs disk image extraction is supported. BlobFS
and FVM are currently not supported.

## Extraction over serial

When storage is corrupted, the system might not boot or sshd won’t come up
because the partition containing binary or keys are corrupted. To help extract
such corrupted storage, fshost host has an option to extract filesystem metadata
and log it to serial log. If the device has serial access, you may get serial
log and get access to the extracted disk image.

Extraction over serial is only enabled for MinFS on userdebug builds for certain
boards. You can enable extraction for a certain board or build by setting
[extract_minfs_metadata_on_corruption](/src/storage/fshost/BUILD.gn) to `true`.

### Symptom of a storage issue

It is difficult to tell if the filesystems of a device have gone bad
by just looking at the device. If your device boots into recovery mode or
displays a gray screen and a power cycle does not resolve the issue, this may
indicate a bad filesystem. However, this process doesn't always indicate a
problem.

### Steps for users to report an issue

Please do not factory reset or flash the device if you suspect a storage related
issue. The following can be done to collect logs:
  * Power off the target.
  * Attach target to a host over serial.
  * Run `fx serial` in a terminal and choose the right device when prompted.
    Read usage details with `fx serial --help`.
  * Power on the target
  * Wait for a few minutes so that the target stops
    dumping extracted image to serial.
  * Send the output of `fx serial` to the local storage team.
  * Wait for confirmation from someone in local storage before resetting or
    flashing the device.

### Scraping the log

The extracted disk image is dumped to log as a series of ASCII characters. This
can lead to a large log for the dump. syslog and serial on the target device may
drop a few log messages due to rate limit. This makes gathering artifacts prone
to errors. So make sure that you have all the data that you need before asking
users to reset or flash their devices.

There is an extension for fx,
[disk-extract-serial-log](/tools/devshell/contrib/disk-extract-serial-log) which
might help you scrape the extract disk image from the serial log with the
following command which rebuilds extracted disk image from serial.log that
contains the serial log and writes the image to extracted-disk.img.

```
fx disk-extract-serial-log --input serial.log --output extracted-disk.img
```

Run `fx disk-extract-serial-log --help` for usage details.

Things to keep in mind if you are manually scraping logs:
  * Messages may arrive out of order.
  * Messages may get dropped for various reasons.
  * Two or more messages might be appear on same line.
  * Each extraction log message contains a string "EIL". See
    [DumpMetadataOptions](/src/storage/fshost/extract-metadata.h)
  * Extraction logs start with message that read
    "EIL: Extracting minfs to serial."
  * Extraction ends with message that read
    "EIL: Done extracting minfs to serial"
  * Pay attention to a line the describes the format of the log message that
    reads something like
    "EIL: Compression:off Checksum:on Offset:on bytes_per_line:64".
    What that means is
      * Extracted image is *not* compressed before dumping to logs.
      * You should see a checksum of the extracted image just before the end
        of the logs.
      * Number of bytes dumped per line is 64. So there should be 128
        hex-characters per line.

## Tool

If you have control over the device environment, you can extract the disk image
by runnig [disk-extract](/src/storage/extractor/bin/BUILD.gn).

## Library

The extractor library is generic enough to be useful to extract any filesystem
or fvm metadata. You need to write a plugin that understands target storage's
disk layout and dumps relevant information. See an example for the MinFS
extractor at [/src/storage/extractor/cpp/minfs_extractor.cc](/src/storage/extractor/cpp/minfs_extractor.cc).

## Future work

You can convert the extracted image back into a blown-up file, which can be
attached to a vm as a disk or attached as a loopback device. This might help
improve debuggability.
