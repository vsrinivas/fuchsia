// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Keep all consts and type defs for completeness.
#![allow(dead_code)]

use zerocopy::{LittleEndian, U32, U64};

pub type LE32 = U32<LittleEndian>;
pub type LE64 = U64<LittleEndian>;
//
// 5.5.2 Virtqueues
//
pub const INFLATEQ: u16 = 0;
pub const DEFLATEQ: u16 = 1;
pub const STATSQ: u16 = 2;

// 5.5.6 Device Operation
// To supply memory to the balloon (aka. inflate):
// (a) The driver constructs an array of addresses of unused memory pages. These addresses are
// divided by 4096 and the descriptor describing the resulting 32-bit array is added to the inflateq
//
// 4096 is historical, and independent of the guest page size.
pub const PAGE_SIZE: usize = 4096;
