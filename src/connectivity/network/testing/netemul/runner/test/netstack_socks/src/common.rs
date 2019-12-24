// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_netemul_sync::{BusMarker, Event, SyncManagerMarker},
    fuchsia_component::client,
};

const BUS_NAME: &str = "evt_bus";
pub const SERVER_READY: i32 = 1;

pub struct BusConnection {
    bus: fidl_fuchsia_netemul_sync::BusProxy,
}

impl BusConnection {
    pub fn new(client: &str) -> Result<BusConnection, Error> {
        let busm = client::connect_to_service::<SyncManagerMarker>()
            .context("SyncManager not available")?;
        let (bus, busch) = fidl::endpoints::create_proxy::<BusMarker>()?;
        busm.bus_subscribe(BUS_NAME, client, busch)?;

        Ok(BusConnection { bus })
    }

    pub fn publish_code(&self, code: i32) -> Result<(), Error> {
        self.bus.publish(Event { code: Some(code), message: None, arguments: None })?;
        Ok(())
    }

    pub async fn wait_for_event(&self, code: i32) -> Result<(), Error> {
        let _ = self
            .bus
            .wait_for_event(Event { code: Some(code), message: None, arguments: None }, 0)
            .await?;
        Ok(())
    }
}
