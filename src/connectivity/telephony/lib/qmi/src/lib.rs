// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::{create_endpoints, ServerEnd};
use fidl_fuchsia_hardware_telephony_transport::QmiProxy;
use fidl_fuchsia_telephony_snoop::PublisherMarker as QmiSnoopMarker;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use std::fs::File;

/// Connect to transport driver, and pass a channel handle for snoop Qmi messages
pub async fn connect_snoop_channel(
    device: &File,
) -> Result<ServerEnd<QmiSnoopMarker>, anyhow::Error> {
    let qmi_channel: fasync::Channel = fasync::Channel::from_channel(fdio::clone_channel(device)?)?;
    let interface = QmiProxy::new(qmi_channel);
    let (client_side, server_side) = create_endpoints::<QmiSnoopMarker>()?;
    match interface.set_snoop_channel(client_side).await {
        Ok(_r) => Ok(server_side),
        Err(e) => Err(e.into()),
    }
}

/// Connect to transport driver, and pass a channel handle for Tx/Rx Qmi messages
/// to/from ril-qmi
pub async fn connect_transport_device(device: &File) -> Result<zx::Channel, anyhow::Error> {
    let qmi_channel: fasync::Channel = fasync::Channel::from_channel(fdio::clone_channel(device)?)?;
    let interface = QmiProxy::new(qmi_channel);
    // create a new channel
    let (client_side, server_side) = zx::Channel::create()?;
    match interface.set_channel(server_side).await {
        Ok(_r) => Ok(client_side),
        Err(e) => Err(e.into()),
    }
}

pub async fn set_network_status(device: &File, state: bool) -> Result<(), anyhow::Error> {
    let qmi_channel: fasync::Channel = fasync::Channel::from_channel(fdio::clone_channel(device)?)?;
    let interface = QmiProxy::new(qmi_channel);
    interface.set_network(state).await.map_err(Into::into)
}
