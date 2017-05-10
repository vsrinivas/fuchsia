# Fuchsia Merkle Roots

[Merkle Trees][merkletree] are used in various places in the Fuchsia ecosystem,
including the [FAR Archive Format][far], the Blob Storage Filesystem, and the
[Package Manager][pm].

In [Magenta][magenta] `mx-verity` provides an API for application components to
read data from local storage. When retrieving data the integrity of the data is
verified and causing reads to fail when the data has been modified or corrupted.
mx-verity is based on a [Merkle Tree][merkletree], and is derived form a similar
system in [Chrome OS][dmverity].

All of these implementations share the algorithm documented herein.

## Parameters of the Merkle Tree

 * Block size: 8kb, 0 padded.
 * Root digest size: 32 bytes.
 * Hash algorithm: SHA-256.
 * Block digest computation: SHA-256((offset | level) + data)

## Definitions

The merkle tree contains levels. A level is a row of the tree, starting at 0 and
counting upward. Level 0 represents the level that contains hashes of chunks of
the input stream.

Each level contains a number of hashes of the previous level. The hashes within
a level are computed from 8kb blocks from the previous level (or data, if level
0), prepended with a block identity.

A block identity is the binary OR of the starting byte index of the block within
the current level, and the current level index.

## Computation of a level

 1. Initialize the level with an index, an offset starting at 0, and an empty
    list of hashes.
 2. For each 8kb of input, compute the next block identity by taking the binary
    OR of the the level index and the current offset.
 3. Take the SHA-256 hash of the identity and 8kb of input data, and append it
    to the levels list of hashes. Increment the offset by 32.
 4. Repeat 1-3 until all input is consumed, padding the last input block with 0
    if it does not align on 8kb.
 5. If the length of hashes is 32, finish.
 6. If the length of hashes is not 8kb aligned, 0 fill up to an 8kb alignment.

## Computation of a root digest

Compute level 0 with the input data.  Construct and compute subsequent levels
using the previous level hashes as input data, until a level hashes contains
exactly 32 bytes. This last level contains the root digest of the merkle tree.

## A note about the empty digest

As a special case, when there is no input data, implementations may need to
handle the calculation independently. The digest of the empty input is simply
the SHA-256 of 8 0 bytes, the block identity of a single 0 length block.

## Example values

 * The empty digest:
 `af5570f5a1810b7af78caf4bc70a660f0df51e42baf91d4de5b2328de0e83dfc`
 * 8192 bytes of `0xff` - "oneblock"
 `85a54736b35f5bc8ed6b1832f01faf3d6448f24fefa7054331a5e9bc16036b32`
 * 65536 bytes of `0xff` - "small"
 `733ac7663521c2aadf131471b3ada067b0d29366ad258737c08d855398304d03`
 * 2105344 bytes of `0xff` - "large"
 `26af21232d940f91ab8a44e5136255230fe04732d3718009130e7bc514bdd480`
 * 2109440 bytes of `0xff` - "unaligned"
 `ec80578cb472963f0986fc4b079678fe727ec6941527f691d2d7fa0c1a7797e3`
 * `0xff0080` bytes filled with repetitions of `0xff0080` - "fuchsia"
 `25b19153c5175b5bb20faafadda0d3712403c4e93370c37d05864f3e6467b9e5`


[merkletree]: https://en.wikipedia.org/wiki/Merkle_tree "Merkle Tree"
[dmverity]: https://www.chromium.org/chromium-os/chromiumos-design-docs/verified-boot "Chrome OS Verified Boot"
[far]: archive_format.md "Archive Format"
[pm]: https://fuchsia.googlesource.com/pm/+/master/README.md "Package Manager"
[magenta]: https://fuchsia.googlesource.com/magenta/+/master/README.md "Magenta"
