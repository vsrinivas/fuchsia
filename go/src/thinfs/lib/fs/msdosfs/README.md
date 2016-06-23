# FAT Filesystem

This directory contains an (in-development) implementation of the FAT16 / FAT32 filesystem.
At the moment, FAT12 is unsupported.

For a more complete reference, refer to a [formal specification of
FAT](https://staff.washington.edu/dittrich/misc/fatgen103.pdf).

## [Boot Sector](bootrecord)

The bootsector of the FAT filesystem acts like a superblock, and provides access
to filesystem metadata. This sector confirms that the FAT volume is properly
formatted, reveals if the volume is FAT16, FAT32, or something else, identifies
locations of clusters on the block device, and identifies other filesystem
metadata.

Reading the boot sector is one of the first steps required when mounting a FAT
filesystem.

## [Direntry & FAT Filenames](direntry)

Support for FAT filenames is complex:
 - Short names are stored using ASCII + a local code page. They are 8.3-length
   filenames, and they consist exclusively of capital letters.
 - Long names are stored preceding short names, and are encoded with UCS-2.
   However, for each long name created, a corresponding short name is also
   created, using an inserted "generation number". A long name is required for
   any name that cannot be reproduced using a short name along.
 - These short and long name combinations may take multiple "DirentrySize"
   chunks, spreading across clusters multiple clusters.

To hide the pain of these abstraction details, the direntry package deals with
both "short" and "long" direntries, and presents a unified view to higher levels
of the filesystem. Since the direntry details are complex, and may require
jumping around the parent directory for additional information, a callback
"GetDirentryCallback" is used to prevent the implementation details of short vs
long direntries from leaking upwards.

In the end, a view of a "Dirent" is provided, with mechanisms to:
  - Look up a Dirent by filename
  - Load a Dirent by index in a directory
  - Serialize an in-memory Dirent into raw bytes

Using these methods does NOT require the caller to have any knowledge of short
vs long filenames. All "input strings" to this package are expected to be UTF-8
encoded. All "output strings" will be UTF-8 encoded as well.

## [Nodes: In-memory Refcounting & Reading / Writing](node)

Writing to a file or directory in any filesystem is complex enough, let alone in
the FAT filesystem. The Node package abstracts away many of the internal
details, and provides a mechanism to prevent code duplication between files and
directories.

The Node interface provides a few important features, including:
 - A simple, per-node reader-writer locking mechanism
 - An implementation of ReaderAt / WriterAt that avoids interacting with the
   underlying cluster layer
 - A mechanism to refcount and automate deletion of nodes. RefUp and RefDown
   should be used to alter the number of EXTERNAL references to nodes (i.e., a
   single new "file handle" indicates RefUp should be called)
 - A mechanism to maintain the hierarchy of nodes via parents and children, as
   well as built-in checks to panic on bad filesystem requests that "should
   never happen"
