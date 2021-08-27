// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{AsBytes, FromBytes, Unaligned};

// Strictly speaking, the MAC address is not defined in 802.11, but it's defined
// here for convenience.
pub type MacAddr = [u8; 6];

// Bssid is a newtype to wrap MacAddr where a BSSID is explicitly required.
#[repr(transparent)]
#[derive(
    FromBytes, AsBytes, Unaligned, Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash,
)]
pub struct Bssid(pub MacAddr);

pub const WILDCARD_BSSID: Bssid = Bssid([0xff, 0xff, 0xff, 0xff, 0xff, 0xff]);
