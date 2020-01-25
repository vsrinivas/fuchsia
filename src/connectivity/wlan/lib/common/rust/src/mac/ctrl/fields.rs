// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::mac::{FrameControl, MacAddr},
    zerocopy::{AsBytes, FromBytes, Unaligned},
};

// IEEE Std 802.11-2016, 9.3.1
// The following fields are always present for every control frame.
#[derive(FromBytes, AsBytes, Unaligned, PartialEq, Eq, Clone, Copy, Debug)]
#[repr(C, packed)]
pub struct CtrlHdr {
    pub frame_ctrl: FrameControl,
    pub duration_or_id: u16,
    pub ra: MacAddr,
}
