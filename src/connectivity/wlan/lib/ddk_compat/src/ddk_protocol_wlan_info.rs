// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(tonyy): This should be eventually replaced with Banjo structs. For now, they just match the
// C layout of Banjo structs.

// LINT.IfChange
#[repr(u8)]
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub enum WlanChannelBandwidth {
    _20 = 0,
    _40 = 1,
    _40Below = 2,
    _80 = 3,
    _160 = 4,
    _80P80 = 5,
}

#[repr(C)]
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub struct WlanChannel {
    pub primary: u8,
    pub cbw: WlanChannelBandwidth,
    pub secondary80: u8,
}
// LINT.ThenChange(//zircon/system/banjo/ddk.protocol.wlan.info/info.banjo)
