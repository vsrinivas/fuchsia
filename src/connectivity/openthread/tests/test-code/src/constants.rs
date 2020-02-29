// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub const OT_RADIO_VERSION_RESP: &[u8; 13] =
    &[0x81, 0x06, 0x02, 0x4f, 0x50, 0x45, 0x4E, 0x54, 0x48, 0x52, 0x45, 0x41, 0x44];
pub const OT_RADIO_VERSION_REQ: &[u8; 3] = &[0x81, 0x02, 0x02];
pub const OT_RADIO_RESET_EVENT: &[u8; 3] = &[0x80, 0x06, 0x0];
pub const OT_RADIO_TX_ALLOWANCE_INIT_VAL: u32 = 4;
pub const OT_RADIO_RX_ALLOWANCE_INIT_VAL: u32 = 4;
pub const OT_RADIO_RX_ALLOWANCE_INC_VAL: u32 = 2;
pub const OT_RADIO_QUERY_NUM: u32 = 128;
pub const OT_RADIO_GET_FRAME_AND_TX_ALLOWANCE: u32 = 2;
pub const OT_RADIO_GET_FRAME: u32 = 1;
