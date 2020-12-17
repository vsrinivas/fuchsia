// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude::*;

use crate::spinel::Subnet;
use anyhow::Error;
use async_trait::async_trait;
use futures::stream::BoxStream;

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub enum NetworkInterfaceEvent {
    InterfaceEnabledChanged(bool),
    AddressWasAdded(Subnet),
    AddressWasRemoved(Subnet),
    RouteToSubnetProvided(Subnet),
    RouteToSubnetRevoked(Subnet),
}

#[async_trait()]
pub trait NetworkInterface: Send + Sync {
    /// Blocks until the network stack has a packet to send.
    async fn outbound_packet_from_stack(&self) -> Result<Vec<u8>, Error>;

    /// Provides the given IPv6 packet to the network stack.
    async fn inbound_packet_to_stack(&self, packet_in: &[u8]) -> Result<(), Error>;

    /// Changes the online status of the network interface. If an interface is
    /// enabled, the stack won't start handling packets until it is marked as online.
    /// This is controlled by the driver.
    async fn set_online(&self, online: bool) -> Result<(), Error>;

    /// Changes the enabled state of the network interface. An interface must
    /// be both online and enabled in order for packets to be handled.
    /// This is generally controlled by the administrator.
    async fn set_enabled(&self, enabled: bool) -> Result<(), Error>;

    /// Adds the given address to this interface.
    // TODO(fxbug.dev/64704): Consider making this method async. This method is
    //       currently synchronous so that it can be used directly from
    //       `Driver::on_prop_value_is`, which is also synchronous.
    fn add_address(&self, addr: &Subnet) -> Result<(), Error>;

    /// Removes the given address from this interface.
    // TODO(fxbug.dev/64704): Consider making this method async. This method is
    //       currently synchronous so that it can be used directly from
    //       `Driver::on_prop_value_is`, which is also synchronous.
    fn remove_address(&self, addr: &Subnet) -> Result<(), Error>;

    /// Indicates to the net stack that this subnet is accessible through this interface.
    // TODO(fxbug.dev/64704): Consider making this method async. This method is
    //       currently synchronous so that it can be used directly from
    //       `Driver::on_prop_value_is`, which is also synchronous.
    fn add_on_link_route(&self, addr: &Subnet) -> Result<(), Error>;

    /// Removes the given subnet from being considered routable over this interface.
    // TODO(fxbug.dev/64704): Consider making this method async. This method is
    //       currently synchronous so that it can be used directly from
    //       `Driver::on_prop_value_is`, which is also synchronous.
    fn remove_on_link_route(&self, addr: &Subnet) -> Result<(), Error>;

    /// Gets the event stream for this network interface.
    ///
    /// Calling this method more than once will cause a panic.
    fn take_event_stream(&self) -> BoxStream<'_, Result<NetworkInterfaceEvent, Error>>;
}

use futures::channel::mpsc;
use parking_lot::Mutex;

pub struct DummyNetworkInterface {
    event_receiver: Mutex<Option<mpsc::Receiver<Result<NetworkInterfaceEvent, Error>>>>,
    event_sender: Mutex<mpsc::Sender<Result<NetworkInterfaceEvent, Error>>>,
}

impl std::fmt::Debug for DummyNetworkInterface {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        f.write_str("DummyNetworkInterface")
    }
}

impl Default for DummyNetworkInterface {
    fn default() -> Self {
        let (event_sender, event_receiver) = mpsc::channel(10);
        DummyNetworkInterface {
            event_receiver: Mutex::new(Some(event_receiver)),
            event_sender: Mutex::new(event_sender),
        }
    }
}

#[async_trait]
impl NetworkInterface for DummyNetworkInterface {
    async fn outbound_packet_from_stack(&self) -> Result<Vec<u8>, Error> {
        futures::future::pending().await
    }

    async fn inbound_packet_to_stack(&self, packet: &[u8]) -> Result<(), Error> {
        fx_log_info!("Packet to Stack: {}", hex::encode(packet));
        Ok(())
    }

    async fn set_online(&self, online: bool) -> Result<(), Error> {
        fx_log_info!("Interface online: {:?}", online);
        Ok(())
    }

    async fn set_enabled(&self, enabled: bool) -> Result<(), Error> {
        fx_log_info!("Interface enabled: {:?}", enabled);
        self.event_sender
            .lock()
            .try_send(Ok(NetworkInterfaceEvent::InterfaceEnabledChanged(enabled)))?;
        Ok(())
    }

    fn add_address(&self, addr: &Subnet) -> Result<(), Error> {
        fx_log_info!("Address Added: {:?}", addr);
        Ok(())
    }

    fn remove_address(&self, addr: &Subnet) -> Result<(), Error> {
        fx_log_info!("Address Removed: {:?}", addr);
        Ok(())
    }

    fn add_on_link_route(&self, addr: &Subnet) -> Result<(), Error> {
        fx_log_info!("On Mesh Route Added: {:?}", addr);
        Ok(())
    }

    fn remove_on_link_route(&self, addr: &Subnet) -> Result<(), Error> {
        fx_log_info!("On Mesh Route Removed: {:?}", addr);
        Ok(())
    }

    fn take_event_stream(&self) -> BoxStream<'_, Result<NetworkInterfaceEvent, Error>> {
        self.event_receiver.lock().take().expect("take_event_stream called twice").boxed()
    }
}
