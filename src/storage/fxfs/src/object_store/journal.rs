// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The journal is implemented as an ever extending file which contains variable length records that
// describe mutations to be applied to various objects.  The journal file consists of blocks, with a
// checksum at the end of each block, but otherwise it can be considered a continuous stream.  The
// checksum is seeded with the checksum from the previous block.  To free space in the journal,
// records are replaced with sparse extents when it is known they are no longer needed to mount.  At
// mount time, the journal is replayed: the mutations are applied into memory.  Eventually, a
// checksum failure will indicate no more records exist to be replayed, at which point the mount can
// continue and the journal will be extended from that point with further mutations as required.
//
// The super-block contains the starting offset and checksum for the journal file and sufficient
// information to locate the initial extents for the journal.  The super-block is written using the
// same per-block checksum that is used for the journal file.

mod writer;

use {
    byteorder::{ByteOrder, LittleEndian},
    serde::{Deserialize, Serialize},
};

// After replaying the journal, it's possible that the stream doesn't end cleanly, in which case the
// next journal block needs to indicate this.  This is done by setting its checksum to what it
// should be xored with this value.
const RESET_XOR: u64 = 0xffffffffffffffff;

type Checksum = u64;

// To keep track of offsets within a journal file, we need both the file offset and the check-sum of
// the preceding block, since the check-sum of the preceding block is an input to the check-sum of
// every block.
#[derive(Clone, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
struct JournalCheckpoint {
    file_offset: u64,

    // Starting check-sum for block that contains file_offset i.e. the checksum for the previous
    // block.
    checksum: Checksum,
}

impl JournalCheckpoint {
    fn new(file_offset: u64, checksum: Checksum) -> JournalCheckpoint {
        JournalCheckpoint { file_offset, checksum }
    }
}

// All journal blocks are covered by a fletcher64 checksum as the last 8 bytes in a block.
fn fletcher64(buf: &[u8], previous: u64) -> u64 {
    assert!(buf.len() % 4 == 0);
    let mut lo = previous as u32;
    let mut hi = (previous >> 32) as u32;
    for chunk in buf.chunks(4) {
        lo = lo.wrapping_add(LittleEndian::read_u32(chunk));
        hi = hi.wrapping_add(lo);
    }
    (hi as u64) << 32 | lo as u64
}
