// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211,
    zerocopy::{AsBytes, FromBytes},
};

#[repr(C)]
#[derive(AsBytes, FromBytes, PartialEq, Eq, Clone, Copy, Debug, Default)]
pub struct ReasonCode(pub u16);

impl From<fidl_ieee80211::ReasonCode> for ReasonCode {
    fn from(fidl_reason_code: fidl_ieee80211::ReasonCode) -> ReasonCode {
        ReasonCode(fidl_reason_code as u16)
    }
}

impl From<ReasonCode> for Option<fidl_ieee80211::ReasonCode> {
    fn from(reason_code: ReasonCode) -> Option<fidl_ieee80211::ReasonCode> {
        fidl_ieee80211::ReasonCode::from_primitive(reason_code.0)
    }
}
