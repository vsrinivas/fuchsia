// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![feature(async_await, await_macro, futures_api)]

use fidl_fuchsia_hardware_telephony_transport::QmiProxy;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use std::fs::File;

pub async fn connect_transport_device(device: &File) -> Result<zx::Channel, failure::Error> {
    let qmi_channel: fasync::Channel = fasync::Channel::from_channel(fdio::clone_channel(device)?)?;
    let interface = QmiProxy::new(qmi_channel);
    // create a new channel
    let (client_side, server_side) = zx::Channel::create()?;
    match await!(interface.set_channel(server_side)) {
        Ok(_r) => Ok(client_side),
        Err(e) => Err(e.into()),
    }
}

pub async fn set_network_status(device: &File, state: bool) -> Result<(), failure::Error> {
    let qmi_channel: fasync::Channel = fasync::Channel::from_channel(fdio::clone_channel(device)?)?;
    let interface = QmiProxy::new(qmi_channel);
    await!(interface.set_network(state)).map_err(Into::into)
}
