// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Client library for the `fuchsia.netemul.sync` FIDL library.

use fidl_fuchsia_netemul_sync as fnetemul_sync;
use futures_util::TryStreamExt as _;

#[allow(missing_docs)]
#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("clients not present on bus: {0:?}")]
    ClientsNotPresentOnBus(Vec<String>),
    #[error("failed to observe expected events: {0:?}")]
    FailedToObserveEvents(Vec<Event>),
    #[error("error communicating with the sync-manager: {0:?}")]
    Fidl(#[from] fidl::Error),
    #[error("error connecting to protocol: {0:?}")]
    ConnectToProtocol(anyhow::Error),
}

type Result<T = ()> = std::result::Result<T, Error>;

/// An event published on a bus.
#[derive(Debug, PartialEq)]
pub struct Event {
    /// User-defined event code.
    pub code: i32,
    /// An optional description of the event.
    pub message: Option<String>,
    /// An optional collection of serialized arguments.
    pub arguments: Option<Vec<u8>>,
}

impl Event {
    /// Creates an event with the specified event code.
    pub fn from_code(code: i32) -> Self {
        Self { code, message: None, arguments: None }
    }
}

impl From<Event> for fnetemul_sync::Event {
    fn from(Event { code, message, arguments }: Event) -> Self {
        Self { code: Some(code), message, arguments, ..Self::EMPTY }
    }
}

impl From<fnetemul_sync::Event> for Event {
    fn from(event: fnetemul_sync::Event) -> Self {
        let fnetemul_sync::Event { code, message, arguments, .. } = event;
        Self { code: code.expect("code not set in event"), message, arguments }
    }
}

/// A connection to a named bus.
///
/// A bus is a broadcast pub/sub network that distributes events.
pub struct Bus {
    bus: fnetemul_sync::BusProxy,
}

impl Bus {
    /// Subscribes to bus `name` with client name `client`.
    pub fn subscribe(name: &str, client: &str) -> Result<Self> {
        let sync_manager =
            fuchsia_component::client::connect_to_protocol::<fnetemul_sync::SyncManagerMarker>()
                .map_err(Error::ConnectToProtocol)?;
        let (bus, server_end) = fidl::endpoints::create_proxy::<fnetemul_sync::BusMarker>()?;
        sync_manager.bus_subscribe(name, client, server_end)?;
        Ok(Bus { bus })
    }

    /// Publishes an event on the bus.
    pub fn publish(&self, event: Event) -> Result {
        self.bus.publish(event.into())?;
        Ok(())
    }

    /// Waits for the specified client to join the bus.
    pub async fn wait_for_client(&self, client: &str) -> Result {
        let (success, absent) =
            self.bus.wait_for_clients(&mut std::iter::once(client), /* no timeout */ 0).await?;
        if !success {
            let absent = absent.expect("absent clients not set in response");
            return Err(Error::ClientsNotPresentOnBus(absent));
        }
        Ok(())
    }

    /// Waits for the specified events to be observed on the bus.
    pub async fn wait_for_events(&self, mut events: Vec<Event>) -> Result {
        let mut stream = self.bus.take_event_stream();
        while let Some(event) = stream.try_next().await? {
            match event {
                fnetemul_sync::BusEvent::OnBusData { data } => {
                    let received_event = data.into();
                    if events.contains(&received_event) {
                        events.retain(|event| event != &received_event);
                        if events.is_empty() {
                            return Ok(());
                        }
                    }
                }
                _ => {}
            }
        }
        if !events.is_empty() {
            return Err(Error::FailedToObserveEvents(events));
        }
        Ok(())
    }
}
