# Blobfs: A content-addressable filesystem for Fuchsia

Blobfs is a filesystem for Fuchsia that stores all files according to their hashes. This is used for
all binaries and other program-related data for Fuchsia. It is not used for storing dynamic data and
it has no hierarchy ([minfs](/docs/concepts/filesystems/minfs.md) handles this case).

The structure of blobfs is described in the [main
documentation](/docs/concepts/filesystems/blobfs.md). This page documents some internal details.

## Version notes

Blobfs follows the [storage versioning scheme](/src/storage/docs/versioning.md). However, there
were some versions before this was adopted that do not follow this scheme.

In major version 8, blobfs used "padded" Merkle trees. In this format, the Merkle tree was padded out
to the nearest block boundary and immediately preceeded the blob data on the block device.

In major version 9, blobfs switched to "compact" Merkle trees where the Merkle tree data immediately
follows the blob data and neither the offset nor the size is block-padded. This saves space. This
change was made when the minor version was treated as a monotonically increasing "revision" and
was not reset. As of this writing the format is controlled by a build flag so some builds will have
major version 8, while others will have major version 9.

Across versions 8 and 9 are the following minor versions:

  * 8.1, 9.1: The initial minor version.
  * 8.2, 9.2: Introduced a backup superblock when running under FVM.
  * 8.3, 9.3: Removed support for ZSTD seekable compression.
