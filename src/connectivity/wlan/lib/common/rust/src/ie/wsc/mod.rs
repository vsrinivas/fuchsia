// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod constants;
mod fields;
mod id;
mod parse;
mod reader;

pub use {constants::*, fields::*, id::*, parse::*, reader::*};

use {
    crate::big_endian::BigEndianU16,
    zerocopy::{AsBytes, FromBytes, Unaligned},
};

#[repr(C, packed)]
#[derive(AsBytes, FromBytes, Unaligned)]
pub struct AttributeHeader {
    id: Id,
    body_len: BigEndianU16,
}
