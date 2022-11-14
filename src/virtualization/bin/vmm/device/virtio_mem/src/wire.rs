// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Keep all consts and type defs for completeness.
#![allow(dead_code)]

pub use zerocopy::byteorder::little_endian::{U16 as LE16, U32 as LE32, U64 as LE64};

// 5.15.1 Virtqueues
//
pub const GUESTREQUESTQ: u16 = 0;
