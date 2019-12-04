// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_zircon as zx, std::ffi::c_void};

/// Proxy to C++ channel scheduler
#[repr(C)]
pub struct CppChannelScheduler {
    chan_sched: *mut c_void,
    /// Make sure that MLME remains on the main channel until deadline
    ensure_on_channel: extern "C" fn(chan_sched: *mut c_void, end: zx::sys::zx_time_t),
}

impl CppChannelScheduler {
    pub fn ensure_on_channel(&self, end: zx::sys::zx_time_t) {
        (self.ensure_on_channel)(self.chan_sched, end);
    }
}

#[cfg(test)]
pub struct FakeCppChannelScheduler {
    pub ensure_on_channel: Option<zx::sys::zx_time_t>,
}

#[cfg(test)]
impl FakeCppChannelScheduler {
    pub fn new() -> Self {
        Self { ensure_on_channel: None }
    }

    pub extern "C" fn ensure_on_channel(chan_sched: *mut c_void, end: zx::sys::zx_time_t) {
        assert!(!chan_sched.is_null());
        unsafe {
            (*(chan_sched as *mut Self)).ensure_on_channel = Some(end);
        }
    }

    pub fn as_chan_sched(&mut self) -> CppChannelScheduler {
        CppChannelScheduler {
            chan_sched: self as *mut Self as *mut c_void,
            ensure_on_channel: Self::ensure_on_channel,
        }
    }
}
