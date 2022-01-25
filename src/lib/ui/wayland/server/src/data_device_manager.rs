// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    crate::seat::Seat,
    anyhow::Error,
    wayland::{
        WlDataDevice, WlDataDeviceManager, WlDataDeviceManagerRequest, WlDataDeviceRequest,
        WlDataSource, WlDataSourceRequest,
    },
};

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
        _this: ObjectRef<Self>,
        request: WlDataDeviceManagerRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlDataDeviceManagerRequest::CreateDataSource { id } => {
                id.implement(client, DataSource)?;
            }
            WlDataDeviceManagerRequest::GetDataDevice { id, seat } => {
                id.implement(client, DataDevice(seat.into()))?;
            }
        }
        Ok(())
    }
}

struct DataSource;

impl RequestReceiver<WlDataSource> for DataSource {
    fn receive(
        this: ObjectRef<Self>,
        request: WlDataSourceRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlDataSourceRequest::Destroy => {
                client.delete_id(this.id())?;
            }
            WlDataSourceRequest::Offer { .. } => {}
            WlDataSourceRequest::SetActions { .. } => {}
        }
        Ok(())
    }
}

struct DataDevice(ObjectRef<Seat>);

impl RequestReceiver<WlDataDevice> for DataDevice {
    fn receive(
        this: ObjectRef<Self>,
        request: WlDataDeviceRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlDataDeviceRequest::Release => {
                client.delete_id(this.id())?;
            }
            WlDataDeviceRequest::StartDrag { .. } => {}
            WlDataDeviceRequest::SetSelection { .. } => {}
        }
        Ok(())
    }
}
