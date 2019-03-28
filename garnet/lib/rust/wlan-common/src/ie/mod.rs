// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod id;
pub mod rsn;

mod constants;
mod fields;
mod parse;
mod reader;
mod write;

use zerocopy::{AsBytes, FromBytes, Unaligned};

pub use {constants::*, fields::*, parse::*, reader::Reader, write::*};

#[repr(C, packed)]
#[derive(Eq, PartialEq, Hash, AsBytes, FromBytes, Unaligned, Copy, Clone, Debug)]
pub struct Id(u8);

impl Id {
    pub fn from_raw(id: u8) -> Self {
        Id(id)
    }

    pub fn raw(self) -> u8 {
        self.0
    }
}

#[repr(C, packed)]
#[derive(AsBytes, FromBytes, Unaligned)]
pub struct Header {
    pub id: Id,
    pub body_len: u8,
}
