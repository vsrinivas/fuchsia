// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211,
    zerocopy::{AsBytes, FromBytes},
};

#[repr(C)]
#[derive(AsBytes, FromBytes, PartialEq, Eq, Clone, Copy, Debug, Default)]
pub struct StatusCode(pub u16);

impl From<fidl_ieee80211::StatusCode> for StatusCode {
    fn from(fidl_status_code: fidl_ieee80211::StatusCode) -> StatusCode {
        StatusCode(fidl_status_code as u16)
    }
}

impl From<StatusCode> for Option<fidl_ieee80211::StatusCode> {
    fn from(status_code: StatusCode) -> Option<fidl_ieee80211::StatusCode> {
        fidl_ieee80211::StatusCode::from_primitive(status_code.0)
    }
}
