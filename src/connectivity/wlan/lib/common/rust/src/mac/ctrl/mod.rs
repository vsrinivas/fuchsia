// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{ByteSlice, LayoutVerified};

mod fields;

pub use fields::*;

#[derive(Debug)]
pub enum CtrlBody<B: ByteSlice> {
    PsPoll { ps_poll: LayoutVerified<B, PsPoll> },
}
