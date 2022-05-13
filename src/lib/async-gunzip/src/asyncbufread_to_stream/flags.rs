// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::DecodeError;
use bitflags::bitflags;

bitflags! {
    /// Gzip header flags.
    ///
    /// See RFC 1952 for detailed information about each flag.
    pub struct Flags: u8 {
        /// An optional indication that the payload is "probably ASCII text".
        const TEXT = 0b0000_0001;

        /// If set, a CRC16 for the gzip header is present.
        const HCRC = 0b0000_0010;

        /// If set, optional "extra" header fields are present.
        ///
        /// _Very_ unlikely to be useful. See here for some additional context:
        /// https://stackoverflow.com/q/65188890/
        const EXTRA = 0b0000_0100;

        /// If set, an "original file name" is present.
        const NAME = 0b0000_1000;

        /// If set, a "file comment" is present, intended for human consumption.
        const COMMENT = 0b0001_0000;
    }
}

impl Flags {
    /// Returns an error if any reserved bit is set.
    pub fn new(flag_byte: u8) -> Result<Self, DecodeError> {
        Self::from_bits(flag_byte).ok_or_else(|| {
            let msg = format!("reserved bit set in gzip flag byte: {flag_byte:08b}");
            DecodeError::Header(msg)
        })
    }
}
