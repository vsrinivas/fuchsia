// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use byteorder::{ByteOrder, LittleEndian};

/// For the foreseeable future, Fxfs will use 64-bit checksums.
pub type Checksum = u64;

/// Generates a Fletcher64 checksum of |buf| seeded by |previous|.
///
/// All logfile blocks are covered by a fletcher64 checksum as the last 8 bytes in a block.
///
/// We also use this checksum for integrity validation of potentially out-of-order writes
/// during Journal replay.
pub fn fletcher64(buf: &[u8], previous: Checksum) -> Checksum {
    assert!(buf.len() % 4 == 0);
    let mut lo = previous as u32;
    let mut hi = (previous >> 32) as u32;
    for chunk in buf.chunks(4) {
        lo = lo.wrapping_add(LittleEndian::read_u32(chunk));
        hi = hi.wrapping_add(lo);
    }
    (hi as u64) << 32 | lo as u64
}
