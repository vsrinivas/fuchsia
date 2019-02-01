// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use wayland::{WlDataDeviceManager, WlDataDeviceManagerRequest};

use crate::client::Client;
use crate::object::{ObjectRef, RequestReceiver};

/// An implementation of the wl_data_device_manager global.
pub struct DataDeviceManager;

impl DataDeviceManager {
    /// Creates a new `DataDeviceManager`.
    pub fn new() -> Self {
        DataDeviceManager
    }
}

impl RequestReceiver<WlDataDeviceManager> for DataDeviceManager {
    fn receive(
        _this: ObjectRef<Self>, request: WlDataDeviceManagerRequest, _client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlDataDeviceManagerRequest::CreateDataSource { .. } => {}
            WlDataDeviceManagerRequest::GetDataDevice { .. } => {}
        }
        Ok(())
    }
}
