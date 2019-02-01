// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_netemul_bus::{BusManagerMarker, BusMarker, Event},
    fuchsia_app::client,
    futures::TryStreamExt,
};

const BUS_NAME: &str = "evt_bus";
pub const SERVER_READY: i32 = 1;

pub struct BusConnection {
    bus: fidl_fuchsia_netemul_bus::BusProxy,
}

impl BusConnection {
    pub fn new(client: &str) -> Result<BusConnection, Error> {
        let busm =
            client::connect_to_service::<BusManagerMarker>().context("BusManager not available")?;
        let (bus, busch) = fidl::endpoints::create_proxy::<BusMarker>()?;
        busm.subscribe(BUS_NAME, client, busch)?;

        Ok(BusConnection { bus })
    }

    pub fn publish_code(&self, code: i32) -> Result<(), Error> {
        self.bus.publish(&mut Event {
            code: code,
            message: None,
            arguments: None,
        })?;
        Ok(())
    }

    pub async fn wait_for_event(&self, code: i32) -> Result<(), Error> {
        let mut stream = self
            .bus
            .take_event_stream()
            .try_filter_map(|event| match event {
                fidl_fuchsia_netemul_bus::BusEvent::OnBusData { data } => {
                    if data.code == code {
                        futures::future::ok(Some(()))
                    } else {
                        futures::future::ok(None)
                    }
                }
                _ => futures::future::ok(None),
            });
        await!(stream.try_next())?;
        Ok(())
    }
}
