// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_wlan_device as wlan;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use std::fs::File;

pub fn connect_wlanphy_device(device: &File) -> Result<wlan::PhyProxy, anyhow::Error> {
    let (local, remote) = zx::Channel::create()?;

    let connector_channel = fdio::clone_channel(device)?;
    let connector = wlan::ConnectorProxy::new(fasync::Channel::from_channel(connector_channel)?);
    connector.connect(ServerEnd::new(remote))?;

    Ok(wlan::PhyProxy::new(fasync::Channel::from_channel(local)?))
}
