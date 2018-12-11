// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error};
use fidl_fuchsia_wlan_device_service::{DeviceServiceMarker, DeviceServiceProxy};
use fuchsia_app::client::connect_to_service;
use parking_lot::RwLock;

// WlanFacade: proxies commands from sl4f test to proper fidl APIs
//
// This object is shared among all threads created by server.  The inner object is the facade
// itself.  Callers interact with a wrapped version of the facade that enforces read/write
// protection.
//
// Use: Create once per server instantiation.
#[derive(Debug)]
struct InnerWlanFacade {
    scan_results: bool
}

#[derive(Debug)]
pub struct WlanFacade {
    wlan_svc: DeviceServiceProxy,

    inner: RwLock<InnerWlanFacade>
}

impl WlanFacade {
    pub fn new() -> Result<WlanFacade, Error> {
        let wlan_svc = connect_to_service::<DeviceServiceMarker>()?;

        Ok(WlanFacade {
            wlan_svc,
            inner: RwLock::new(
                InnerWlanFacade {
                    scan_results: false
                })
        })
    }
}
