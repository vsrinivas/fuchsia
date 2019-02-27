# gzip-header

A library to decode and encode headers for the [gzip format](http://www.gzip.org/zlib/rfc-gzip.html).
The library also contains a reader absctraction over a CRC checksum hasher.

A file in the gzip format contains a gzip header, a number of compressed data blocks in the [DEFLATE](http://www.gzip.org/zlib/rfc-deflate.html) format, and ends with the CRC32-checksum (in the IEEE format) and number of bytes (modulo `2^32`) of the uncompressed data.

The gzip header is purely a set of metadata, and doesn't have any impact on the decoding of the compressed data other than the fact that `DEFLATE`-encoded data with a gzip-header is checked using the CRC32 algorithm.

This library is based on the gzip header functionality in the [flate2](https://crates.io/crates/flate2) crate.

# License

Like the non-C parts of `flate2-rs`, `gzip-header` is distributed under the terms of both the MIT license and the Apache License (Version 2.0),

See LICENSE-APACHE, and LICENSE-MIT for details.
