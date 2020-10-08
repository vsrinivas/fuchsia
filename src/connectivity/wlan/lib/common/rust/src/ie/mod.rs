// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod fake_ies;
pub mod intersect;
pub mod rsn;
pub mod wpa;
pub mod wsc;

mod constants;
mod fields;
mod id;
mod parse;
mod rates_writer;
mod reader;
mod write;

use zerocopy::{AsBytes, FromBytes, Unaligned};

pub use {
    constants::*, fake_ies::*, fields::*, id::*, intersect::*, parse::*, rates_writer::*,
    reader::Reader, write::*,
};

#[repr(C, packed)]
#[derive(AsBytes, FromBytes, Unaligned)]
pub struct Header {
    pub id: Id,
    pub body_len: u8,
}
