// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::utils, fuchsia_zircon::sys as zx, wlan_mlme::common::sequence::SequenceManager};

#[no_mangle]
pub extern "C" fn mlme_sequence_manager_new() -> *mut SequenceManager {
    Box::into_raw(Box::new(SequenceManager::new()))
}

#[no_mangle]
pub extern "C" fn mlme_sequence_manager_delete(mgr: *mut SequenceManager) {
    if !mgr.is_null() {
        unsafe { Box::from_raw(mgr) };
    }
}

#[no_mangle]
pub extern "C" fn mlme_sequence_manager_next_sns1(
    mgr: &mut SequenceManager,
    sta_addr: &[u8; 6],
) -> u32 {
    mgr.next_sns1(sta_addr)
}

#[no_mangle]
pub extern "C" fn mlme_sequence_manager_next_sns2(
    mgr: &mut SequenceManager,
    sta_addr: &[u8; 6],
    tid: u16,
) -> u32 {
    mgr.next_sns2(sta_addr, tid)
}
