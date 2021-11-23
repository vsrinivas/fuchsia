// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;
use std::convert::TryFrom as _;
use std::pin::Pin;

use fidl::endpoints::Proxy as _;
use fidl_fuchsia_hardware_network as fhardware_network;
use fidl_fuchsia_net_debug as fnet_debug;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_net_virtualization as fnet_virtualization;
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_zircon as zx;

use anyhow::{anyhow, Context as _};
use async_trait::async_trait;
use futures::{channel::oneshot, future, FutureExt as _, StreamExt as _, TryStreamExt as _};
use log::{debug, error, info, warn};

use crate::{
    errors::{self, ContextExt as _},
    DeviceClass,
};

/// Wrapper around a `[fnet_virtualization::NetworkRequest]` that allows for the
/// server to notify all existing interfaces to shut down when the client closes
/// the `Network` channel.
#[derive(Debug)]
pub(super) enum NetworkRequest {
    Request(fnet_virtualization::NetworkRequest, future::Shared<oneshot::Receiver<()>>),
    Finished(oneshot::Sender<()>),
}

pub(super) enum Event {
    ControlRequestStream(fnet_virtualization::ControlRequestStream),
    ControlRequest(fnet_virtualization::ControlRequest),
    NetworkRequest(NetworkRequest),
    InterfaceClose(u64),
}

impl std::fmt::Debug for Event {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        match self {
            Event::ControlRequestStream(stream) => {
                let _: &fnet_virtualization::ControlRequestStream = stream;
                write!(f, "ControlRequestStream")
            }
            Event::ControlRequest(_) | Event::NetworkRequest(_) | Event::InterfaceClose(_) => {
                write!(f, "{:?}", self)
            }
        }
    }
}

pub(super) type EventStream = Pin<Box<dyn futures::stream::Stream<Item = Event>>>;

// TODO(https://fxbug.dev/88017): `guests` must always be non-empty. Explore using a non-empty set
// type to encode this invariant in the type system.
enum BridgeState {
    Init,
    WaitingForGuests { upstream: u64 },
    WaitingForUpstream { guests: HashSet<u64> },
    Bridged { id: u64, upstream: u64, guests: HashSet<u64> },
}

impl BridgeState {
    fn bridge_id(&self) -> Option<u64> {
        match self {
            BridgeState::Init
            | BridgeState::WaitingForUpstream { guests: _ }
            | BridgeState::WaitingForGuests { upstream: _ } => None,
            BridgeState::Bridged { id, guests: _, upstream: _ } => Some(*id),
        }
    }

    fn upstream(&self) -> Option<u64> {
        match self {
            BridgeState::Init | BridgeState::WaitingForUpstream { guests: _ } => None,
            BridgeState::WaitingForGuests { upstream }
            | BridgeState::Bridged { id: _, guests: _, upstream } => Some(*upstream),
        }
    }

    fn contains_guest(&self, id: u64) -> bool {
        match self {
            BridgeState::Init | BridgeState::WaitingForGuests { upstream: _ } => false,
            BridgeState::WaitingForUpstream { guests }
            | BridgeState::Bridged { id: _, upstream: _, guests } => guests.contains(&id),
        }
    }
}

impl Default for BridgeState {
    fn default() -> Self {
        BridgeState::Init
    }
}

#[async_trait(?Send)]
pub(super) trait Handler {
    async fn handle_event(
        &'async_trait mut self,
        event: Event,
        events: &'async_trait mut futures::stream::SelectAll<EventStream>,
    ) -> Result<(), errors::Error>;

    async fn handle_interface_update_result(
        &mut self,
        update_result: &fnet_interfaces_ext::UpdateResult<'_>,
    ) -> Result<(), errors::Error>;
}

pub(super) struct Virtualization<B: BridgeHandler> {
    installer: fnet_interfaces_admin::InstallerProxy,
    allowed_upstream_device_classes: HashSet<DeviceClass>,
    bridge_handler: B,
    bridge_state: BridgeState,
    candidate_upstream_interfaces: HashSet<u64>,
}

impl<B: BridgeHandler> Virtualization<B> {
    pub fn new(
        allowed_upstream_device_classes: HashSet<DeviceClass>,
        bridge_handler: B,
        installer: fnet_interfaces_admin::InstallerProxy,
    ) -> Self {
        Self {
            installer,
            allowed_upstream_device_classes,
            bridge_handler,
            bridge_state: Default::default(),
            candidate_upstream_interfaces: Default::default(),
        }
    }

    fn is_device_class_allowed_for_upstream(
        &self,
        device_class: fnet_interfaces::DeviceClass,
    ) -> bool {
        match device_class {
            fnet_interfaces::DeviceClass::Device(device_class) => {
                self.allowed_upstream_device_classes.contains(&device_class.into())
            }
            fnet_interfaces::DeviceClass::Loopback(fnet_interfaces::Empty {}) => false,
        }
    }

    async fn handle_control_request(
        &self,
        request: fnet_virtualization::ControlRequest,
        events: &mut futures::stream::SelectAll<EventStream>,
    ) -> Result<(), errors::Error> {
        match request {
            fnet_virtualization::ControlRequest::CreateNetwork {
                config,
                network,
                control_handle: _,
            } => match config {
                fnet_virtualization::Config::Bridged(fnet_virtualization::Bridged { .. }) => {
                    info!("got a request to create a bridged network");
                    // Create a oneshot channel we can use when the `Network` channel is closed, to
                    // notify each interface task to close its corresponding `Interface` channel as
                    // well.
                    let (close_channel_tx, close_channel_rx) = futures::channel::oneshot::channel();
                    let close_channel_rx = close_channel_rx.shared();
                    let stream = network
                        .into_stream()
                        .expect("convert server end into stream")
                        .filter_map(move |request| {
                            future::ready(match request {
                                Ok(request) => {
                                    Some(NetworkRequest::Request(request, close_channel_rx.clone()))
                                }
                                Err(e) => {
                                    error!("network request error: {:?}", e);
                                    None
                                }
                            })
                        })
                        .chain(futures::stream::once(futures::future::ready(
                            NetworkRequest::Finished(close_channel_tx),
                        )));
                    events.push(stream.map(Event::NetworkRequest).boxed());
                }
                config => panic!("unsupported network config type {:?}", config),
            },
        }
        Ok(())
    }

    async fn handle_network_request(
        &mut self,
        request: NetworkRequest,
        events: &mut futures::stream::SelectAll<EventStream>,
    ) -> Result<(), errors::Error> {
        match request {
            NetworkRequest::Request(request, mut network_close_rx) => {
                match request {
                    fnet_virtualization::NetworkRequest::AddPort {
                        port,
                        interface,
                        control_handle: _,
                    } => {
                        // TODO(https://fxbug.dev/87111): send a terminal event on the channel if
                        // the device could not be added to the network due to incompatibility; for
                        // example, if the network is bridged and the device does not support the
                        // same L2 protocol as other devices on the bridge.

                        // Get the device this port belongs to, and install it on the netstack.
                        let (device, server_end) =
                            fidl::endpoints::create_endpoints::<fhardware_network::DeviceMarker>()
                                .context("create endpoints")
                                .map_err(errors::Error::NonFatal)?;
                        let port = port.into_proxy().expect("client end into proxy");
                        port.get_device(server_end)
                            .context("call get device")
                            .map_err(errors::Error::NonFatal)?;

                        let (device_control, server_end) = fidl::endpoints::create_proxy::<
                            fnet_interfaces_admin::DeviceControlMarker,
                        >()
                        .context("create proxy")
                        .map_err(errors::Error::NonFatal)?;
                        self.installer
                            .install_device(device, server_end)
                            .context("call install device")
                            .map_err(errors::Error::Fatal)?;

                        // Create an interface on the device, and enable it.
                        let fhardware_network::PortInfo { id: port_id, .. } = port
                            .get_info()
                            .await
                            .context("get port info")
                            .map_err(errors::Error::NonFatal)?;
                        let mut port_id = port_id
                            .context("port id not included in port info")
                            .map_err(errors::Error::NonFatal)?;
                        let (control, server_end) =
                            fnet_interfaces_ext::admin::Control::create_endpoints()
                                .context("create Control endpoints")
                                .map_err(errors::Error::NonFatal)?;
                        device_control
                            .create_interface(
                                &mut port_id,
                                server_end,
                                fnet_interfaces_admin::Options {
                                    ..fnet_interfaces_admin::Options::EMPTY
                                },
                            )
                            .context("call create interface")
                            .map_err(errors::Error::NonFatal)?;
                        let id = control
                            .get_id()
                            .await
                            .context("call get id")
                            .map_err(errors::Error::NonFatal)?;

                        if !control
                            .enable()
                            .await
                            .context("call enable")
                            .map_err(errors::Error::NonFatal)?
                            .map_err(|e| anyhow!("failed to enable interface: {:?}", e))
                            .map_err(errors::Error::NonFatal)?
                        {
                            warn!("added interface {} was already enabled", id);
                        }

                        // Add this interface to the existing bridge, or create one if none exists.
                        self.add_interface_to_bridge(id)
                            .await
                            .context("adding interface to bridge")?;

                        // Wait for a signal that this interface should be removed from the bridge
                        // and the virtual network.
                        let shutdown_fut = async move {
                            let mut interface_closure = interface
                                .into_stream()
                                .expect("convert server end into stream")
                                .map(|request| {
                                    // `fuchsia.net.virtualization/Interface` is a protocol with no
                                    // methods, so `InterfaceRequest` is an uninstantiable enum.
                                    // This prevents us from exhaustively matching on its variants,
                                    // so we just drop the request here.
                                    request
                                        .map(|_request: fnet_virtualization::InterfaceRequest| ())
                                })
                                .try_collect::<()>();
                            let mut device_control_closure = device_control.on_closed().fuse();
                            let control_termination = control.wait_termination().fuse();
                            futures::pin_mut!(control_termination);
                            let reason = futures::select! {
                                // The interface channel has been closed by the client.
                                result = interface_closure => {
                                    format!("interface channel closed by client: {:?}", result)
                                },
                                // The device has been detached from the netstack.
                                result = device_control_closure => {
                                    match result {
                                        Ok(zx::Signals::CHANNEL_PEER_CLOSED) => {},
                                        result => error!(
                                            "got unexpected result waiting for device control \
                                            channel closure: {:?}",
                                            result,
                                        ),
                                    }
                                    "device detached from netstack".to_string()
                                }
                                // The virtual network has been shut down and is notifying us to
                                // remove the interface.
                                result = network_close_rx => {
                                    let () = result.expect("sender should not be dropped");
                                    "network has been shut down".to_string()
                                },
                                // A terminal event was sent on the interface control channel,
                                // signaling that the interface was removed.
                                terminal_error = control_termination => {
                                    format!(
                                        "interface control channel closed: {:?}",
                                        terminal_error
                                    )
                                }
                            };
                            info!("interface {}: {}, removing interface", id, reason);
                            id
                        };
                        events.push(
                            futures::stream::once(shutdown_fut.map(Event::InterfaceClose)).boxed(),
                        );
                    }
                }
            }
            NetworkRequest::Finished(network_close_tx) => {
                // Close down the network.
                match network_close_tx.send(()) {
                    Ok(()) => {}
                    Err(()) => {
                        info!("removing virtualized network with no devices attached")
                    }
                }
            }
        }
        Ok(())
    }

    async fn add_interface_to_bridge(&mut self, id: u64) -> Result<(), errors::Error> {
        info!("got a request to add interface {} to bridge", id);
        let Self { bridge_state, bridge_handler, .. } = self;
        *bridge_state = match std::mem::take(bridge_state) {
            BridgeState::Init => BridgeState::WaitingForUpstream { guests: HashSet::from([id]) },
            // If a bridge doesn't exist, but we have an interface with upstream connectivity,
            // create the bridge.
            BridgeState::WaitingForGuests { upstream } => {
                let guests = HashSet::from([id]);
                let bridge_id = bridge_handler
                    .build_bridge(guests.iter().cloned(), upstream)
                    .await
                    .context("building bridge")?;
                BridgeState::Bridged { id: bridge_id, upstream, guests }
            }
            // If a bridge doesn't exist, and we don't yet have an interface with upstream
            // connectivity, just keep track of the interface to be bridged, so we can eventually
            // include it in the bridge.
            BridgeState::WaitingForUpstream { mut guests } => {
                assert_eq!(guests.insert(id), true);
                // No change to bridge state.
                BridgeState::WaitingForUpstream { guests }
            }
            // If a bridge already exists, tear it down and create a new one, re-using the interface
            // that has upstream connectivity and including all the interfaces that were bridged
            // previously.
            BridgeState::Bridged { id: bridge_id, upstream, mut guests } => {
                bridge_handler.destroy_bridge(bridge_id).await.context("destroying bridge")?;
                assert_eq!(guests.insert(id), true);
                let bridge_id = bridge_handler
                    .build_bridge(guests.iter().cloned(), upstream)
                    .await
                    .context("building bridge")?;
                BridgeState::Bridged { id: bridge_id, upstream, guests }
            }
        };
        Ok(())
    }

    async fn remove_interface_from_bridge(&mut self, id: u64) -> Result<(), errors::Error> {
        info!("got a request to remove interface {} from bridge", id);
        let Self { bridge_state, bridge_handler, .. } = self;
        *bridge_state = match std::mem::take(bridge_state) {
            BridgeState::Init | BridgeState::WaitingForGuests { .. } => {
                panic!("cannot remove guest interface since it was not previously added")
            }
            BridgeState::WaitingForUpstream { mut guests } => {
                assert_eq!(guests.remove(&id), true);
                if guests.is_empty() {
                    BridgeState::Init
                } else {
                    // No change to bridge state.
                    BridgeState::WaitingForUpstream { guests }
                }
            }
            BridgeState::Bridged { id: bridge_id, upstream, mut guests } => {
                bridge_handler.destroy_bridge(bridge_id).await.context("destroying bridge")?;
                assert_eq!(guests.remove(&id), true);
                if guests.is_empty() {
                    BridgeState::WaitingForGuests { upstream }
                } else {
                    let bridge_id = self
                        .bridge_handler
                        .build_bridge(guests.iter().cloned(), upstream)
                        .await
                        .context("building bridge")?;
                    BridgeState::Bridged { id: bridge_id, upstream, guests }
                }
            }
        };
        Ok(())
    }

    async fn set_bridge_upstream_interface(&mut self, upstream: u64) -> Result<(), errors::Error> {
        info!("found an interface that provides upstream connectivity: {}", upstream);
        let Self { bridge_state, bridge_handler, .. } = self;
        *bridge_state = match std::mem::take(bridge_state) {
            BridgeState::Init | BridgeState::WaitingForGuests { upstream: _ } => {
                BridgeState::WaitingForGuests { upstream }
            }
            BridgeState::WaitingForUpstream { guests } => {
                let bridge_id = self
                    .bridge_handler
                    .build_bridge(guests.iter().cloned(), upstream)
                    .await
                    .context("building bridge")?;
                BridgeState::Bridged { id: bridge_id, upstream, guests }
            }
            // If a bridge already exists, tear it down and create a new one, using this new
            // interface to provide upstream connectivity, and including all the interfaces that
            // were bridged previously.
            BridgeState::Bridged { id: bridge_id, upstream: _, guests } => {
                bridge_handler.destroy_bridge(bridge_id).await.context("destroying bridge")?;
                let bridge_id = self
                    .bridge_handler
                    .build_bridge(guests.iter().cloned(), upstream)
                    .await
                    .context("building bridge")?;
                BridgeState::Bridged { id: bridge_id, upstream, guests }
            }
        };
        Ok(())
    }

    async fn remove_bridge_upstream_interface(&mut self) -> Result<(), errors::Error> {
        let Self { bridge_state, bridge_handler, .. } = self;
        *bridge_state = match std::mem::take(bridge_state) {
            BridgeState::Init | BridgeState::WaitingForUpstream { guests: _ } => {
                panic!("cannot remove upstream interface since it doesn't exist")
            }
            BridgeState::WaitingForGuests { upstream: _ } => BridgeState::Init,
            BridgeState::Bridged { id, upstream: _, guests } => {
                bridge_handler.destroy_bridge(id).await.context("destroying bridge")?;
                BridgeState::WaitingForUpstream { guests }
            }
        };
        Ok(())
    }

    async fn add_candidate_upstream_interface(&mut self, id: u64) -> Result<(), errors::Error> {
        match self.bridge_state {
            BridgeState::Bridged { .. } | BridgeState::WaitingForGuests { .. } => {
                // We already have an upstream interface that provides connectivity; add the
                // interface to the list of candidates for upstream.
                assert_eq!(self.candidate_upstream_interfaces.insert(id), true);
            }
            BridgeState::WaitingForUpstream { .. } | BridgeState::Init => {
                // We don't already have an upstream interface that provides connectivity. Build a
                // bridge with this one.
                self.set_bridge_upstream_interface(id)
                    .await
                    .context("set upstream interface on bridge")?;
            }
        };
        Ok(())
    }
}

#[async_trait(?Send)]
impl<B: BridgeHandler> Handler for Virtualization<B> {
    async fn handle_event(
        &'async_trait mut self,
        event: Event,
        events: &'async_trait mut futures::stream::SelectAll<EventStream>,
    ) -> Result<(), errors::Error> {
        match event {
            Event::ControlRequestStream(stream) => {
                events.push(
                    stream
                        .filter_map(|request| {
                            future::ready(match request {
                                Ok(request) => Some(Event::ControlRequest(request)),
                                Err(e) => {
                                    error!("control request error: {:?}", e);
                                    None
                                }
                            })
                        })
                        .boxed(),
                );
            }
            Event::ControlRequest(request) => self
                .handle_control_request(request, events)
                .await
                .context("handle control request")?,
            Event::NetworkRequest(request) => self
                .handle_network_request(request, events)
                .await
                .context("handle network request")?,
            Event::InterfaceClose(id) => {
                // Remove this interface from the existing bridge.
                self.remove_interface_from_bridge(id)
                    .await
                    .context("removing interface from bridge")?;
            }
        }
        Ok(())
    }

    async fn handle_interface_update_result(
        &mut self,
        update_result: &fnet_interfaces_ext::UpdateResult<'_>,
    ) -> Result<(), errors::Error> {
        match update_result {
            fnet_interfaces_ext::UpdateResult::Added(properties)
            | fnet_interfaces_ext::UpdateResult::Existing(properties) => {
                let fnet_interfaces_ext::Properties { id, online, device_class, .. } = **properties;
                // If this interface is one that was added as part of a virtual network netcfg is
                // managing, it's not a candidate to provide upstream connectivity.
                if self.bridge_state.contains_guest(id)
                    || self.bridge_state.bridge_id() == Some(id)
                    || !online
                    || !self.is_device_class_allowed_for_upstream(device_class)
                {
                    return Ok(());
                }
                // This interface is the device class we are looking for and is online.
                self.add_candidate_upstream_interface(id)
                    .await
                    .context("add candidate interface")?;
            }
            fnet_interfaces_ext::UpdateResult::Changed {
                previous: fnet_interfaces::Properties { online: previously_online, .. },
                current: current_properties,
            } => {
                let fnet_interfaces_ext::Properties { id, online, device_class, .. } =
                    **current_properties;
                // We currently ignore the situation where an interface that is providing upstream
                // connectivity changes from online to offline. The only signal that causes us to
                // destroy an existing bridge is if the interface providing upstream connectivity is
                // removed entirely.

                // If this interface is one that was added as part of a virtual network netcfg is
                // managing, it's not a candidate to provide upstream connectivity.
                if self.bridge_state.contains_guest(id)
                    || self.bridge_state.bridge_id() == Some(id)
                    || *previously_online != Some(false)
                    || !online
                    || !self.is_device_class_allowed_for_upstream(device_class)
                {
                    return Ok(());
                }
                // This interface is the device class we are looking for and just went
                // online.
                self.add_candidate_upstream_interface(id)
                    .await
                    .context("add candidate interface")?;
            }
            fnet_interfaces_ext::UpdateResult::Removed(fnet_interfaces_ext::Properties {
                id,
                ..
            }) => {
                // The removed interface was either a candidate for providing upstream connectivity
                // but was not currently being used, or it did not provide connectivity at all, so
                // ignore its removal.
                if self.candidate_upstream_interfaces.remove(&id)
                    || self.bridge_state.upstream() != Some(*id)
                {
                    return Ok(());
                }
                debug!("interface providing upstream connectivity was removed");
                // This interface was the one providing upstream connectivity. Tear down
                // the bridge, and try to find a replacement.
                match self.candidate_upstream_interfaces.iter().cloned().next() {
                    Some(id) => {
                        assert_eq!(self.candidate_upstream_interfaces.remove(&id), true);
                        self.set_bridge_upstream_interface(id)
                            .await
                            .context("set upstream interface on bridge")?;
                    }
                    None => {
                        self.remove_bridge_upstream_interface()
                            .await
                            .context("removing upstream interface from bridge")?;
                    }
                }
            }
            fnet_interfaces_ext::UpdateResult::NoChange => {}
        }
        Ok(())
    }
}

pub(super) struct Stub;

#[async_trait(?Send)]
impl Handler for Stub {
    async fn handle_event(
        &'async_trait mut self,
        event: Event,
        _events: &'async_trait mut futures::stream::SelectAll<EventStream>,
    ) -> Result<(), errors::Error> {
        panic!("stub handler requested to handle a virtualization event: {:#?}", event)
    }

    async fn handle_interface_update_result(
        &mut self,
        _update_result: &fnet_interfaces_ext::UpdateResult<'_>,
    ) -> Result<(), errors::Error> {
        Ok(())
    }
}

/// An abstraction over the logic involved to instruct the netstack to create or destroy a bridge.
///
/// Allows for testing the virtualization handler by providing an instrumented implementation of
/// `BridgeHandler` in order to observe its behavior.
#[async_trait(?Send)]
pub(super) trait BridgeHandler {
    async fn build_bridge(
        &self,
        interfaces: impl Iterator<Item = u64> + 'async_trait,
        upstream_interface: u64,
    ) -> Result<u64, errors::Error>;

    async fn destroy_bridge(&self, id: u64) -> Result<(), errors::Error>;
}

pub(super) struct BridgeHandlerImpl {
    stack: fnet_stack::StackProxy,
    netstack: fnetstack::NetstackProxy,
    debug: fnet_debug::InterfacesProxy,
}

impl BridgeHandlerImpl {
    pub fn new(
        stack: fnet_stack::StackProxy,
        netstack: fnetstack::NetstackProxy,
        debug: fnet_debug::InterfacesProxy,
    ) -> Self {
        Self { stack, netstack, debug }
    }
}

#[async_trait(?Send)]
impl BridgeHandler for BridgeHandlerImpl {
    async fn build_bridge(
        &self,
        interfaces: impl Iterator<Item = u64> + 'async_trait,
        upstream_interface: u64,
    ) -> Result<u64, errors::Error> {
        let bridge_id = {
            let interfaces: Vec<u32> = interfaces
                .chain(std::iter::once(upstream_interface))
                .map(u32::try_from)
                .collect::<Result<Vec<_>, _>>()
                .context("convert NIC IDs to u32")
                .map_err(errors::Error::Fatal)?;
            let (fnetstack::NetErr { status, message }, bridge) = self
                .netstack
                .bridge_interfaces(&interfaces)
                .await
                .context("call bridge interfaces")
                .map_err(errors::Error::Fatal)?;
            match status {
                fnetstack::Status::Ok => bridge,
                status => {
                    return Err(errors::Error::Fatal(anyhow!(
                        "could not bridge interfaces ({:?}): {}",
                        status,
                        message
                    )))
                }
            }
        };
        let bridge_id = u64::from(bridge_id);

        // Enable the bridge we just created.
        let (control, server_end) = fnet_interfaces_ext::admin::Control::create_endpoints()
            .context("create Control endpoints")
            .map_err(errors::Error::NonFatal)?;
        self.debug
            .get_admin(bridge_id, server_end)
            .context("call get admin")
            .map_err(errors::Error::Fatal)?;
        let did_enable = control
            .enable()
            .await
            .context("call enable")
            .map_err(errors::Error::Fatal)?
            // If we created a bridge but the interface wasn't successfully enabled, the bridging
            // state machine has become inconsistent with the netstack, so we return an
            // unrecoverable error.
            .map_err(|e| anyhow!("failed to enable interface: {:?}", e))
            .map_err(errors::Error::Fatal)?;
        assert!(
            did_enable,
            "the bridge should have been disabled on creation and then enabled by Control.Enable",
        );
        debug!("enabled bridge interface {}", bridge_id);
        Ok(bridge_id)
    }

    async fn destroy_bridge(&self, id: u64) -> Result<(), errors::Error> {
        debug!("tearing down bridge with id {}", id);
        self.stack
            .del_ethernet_interface(id)
            .await
            .context("call del_ethernet_interface")
            .map_err(errors::Error::Fatal)?
            // If the netstack failed to destroy the bridge, the bridging state machine has become
            // inconsistent with the netstack, so we return an unrecoverable error.
            //
            // NB: This does mean that if the bridge interface was manually removed out from under
            // netcfg, we'll panic.
            .map_err(|e| anyhow!("failed to delete ethernet interface: {:?}", e))
            .map_err(errors::Error::Fatal)
    }
}

#[cfg(test)]
mod tests {
    use futures::{channel::mpsc, SinkExt as _};
    use test_case::test_case;

    use super::*;

    #[derive(Debug, PartialEq)]
    enum BridgeEvent {
        Destroyed,
        Created { interfaces: HashSet<u64>, upstream_interface: u64 },
    }

    impl BridgeEvent {
        fn created(interfaces: HashSet<u64>, upstream_interface: u64) -> Self {
            Self::Created { interfaces, upstream_interface }
        }

        fn destroyed() -> Self {
            Self::Destroyed
        }
    }

    struct BridgeHandlerTestImplInner {
        bridge: Option<u64>,
        events: mpsc::Sender<BridgeEvent>,
    }

    struct BridgeHandlerTestImpl {
        inner: std::cell::RefCell<BridgeHandlerTestImplInner>,
    }

    impl BridgeHandlerTestImpl {
        fn new(events: mpsc::Sender<BridgeEvent>) -> Self {
            BridgeHandlerTestImpl {
                inner: std::cell::RefCell::new(BridgeHandlerTestImplInner { bridge: None, events }),
            }
        }
    }

    #[async_trait(?Send)]
    impl BridgeHandler for BridgeHandlerTestImpl {
        async fn destroy_bridge(&self, id: u64) -> Result<(), errors::Error> {
            let BridgeHandlerTestImplInner { bridge, events } = &mut *self.inner.borrow_mut();
            assert_eq!(*bridge, Some(id), "cannot destroy a non-existent bridge");
            *bridge = None;
            events.send(BridgeEvent::Destroyed).await.expect("send event");
            Ok(())
        }

        async fn build_bridge(
            &self,
            interfaces: impl Iterator<Item = u64> + 'async_trait,
            upstream_interface: u64,
        ) -> Result<u64, errors::Error> {
            let BridgeHandlerTestImplInner { bridge, events } = &mut *self.inner.borrow_mut();
            assert_eq!(
                *bridge, None,
                "cannot create a bridge since there is already an existing bridge",
            );
            const BRIDGE_IF: u64 = 99;
            *bridge = Some(BRIDGE_IF);
            events
                .send(BridgeEvent::Created { interfaces: interfaces.collect(), upstream_interface })
                .await
                .expect("send event");
            Ok(BRIDGE_IF)
        }
    }

    const GUEST_IF: u64 = 1;
    const GUEST_IF2: u64 = 2;
    const UPSTREAM_IF: u64 = 11;
    const UPSTREAM_IF2: u64 = 12;

    enum Action {
        AddInterface(u64),
        RemoveInterface(u64),
        SetUpstream(u64),
        RemoveUpstream,
    }

    #[test_case(
        // Verify that we wait to create a bridge until an interface is added to a virtual bridged
        // network.
        [
            (Action::SetUpstream(UPSTREAM_IF), vec![]),
            (
                Action::AddInterface(GUEST_IF),
                vec![BridgeEvent::created([GUEST_IF].into(), UPSTREAM_IF)],
            ),
        ];
        "wait for guest"
    )]
    #[test_case(
        // Verify that we wait to create a bridge until there is an interface that provides upstream
        // connectivity.
        [
            (Action::AddInterface(GUEST_IF), vec![]),
            (
                Action::SetUpstream(UPSTREAM_IF),
                vec![BridgeEvent::created([GUEST_IF].into(), UPSTREAM_IF)],
            ),
        ];
        "wait for upstream"
    )]
    #[test_case(
        // Verify that the bridge is destroyed when the upstream interface is removed and there are
        // no more candidates to provide upstream connectivity.
        [
            (Action::SetUpstream(UPSTREAM_IF), vec![]),
            (
                Action::AddInterface(GUEST_IF),
                vec![BridgeEvent::created([GUEST_IF].into(), UPSTREAM_IF)],
            ),
            (Action::RemoveUpstream, vec![BridgeEvent::destroyed()]),
        ];
        "destroy bridge when no upstream"
    )]
    #[test_case(
        // Verify that when we add multiple interfaces to the virtual network, they are added to the
        // bridge one by one, which is implemented by tearing down the bridge and rebuilding it
        // every time an interface is added or removed.
        [
            (Action::SetUpstream(UPSTREAM_IF), vec![]),
            (
                Action::AddInterface(GUEST_IF),
                vec![BridgeEvent::created([GUEST_IF].into(), UPSTREAM_IF)],
            ),
            (
                Action::AddInterface(GUEST_IF2),
                vec![
                    BridgeEvent::destroyed(),
                    BridgeEvent::created([GUEST_IF, GUEST_IF2].into(), UPSTREAM_IF),
                ],
            ),
            (
                Action::RemoveInterface(GUEST_IF2),
                vec![
                    BridgeEvent::destroyed(),
                    BridgeEvent::created([GUEST_IF].into(), UPSTREAM_IF),
                ],
            ),
            (
                Action::RemoveInterface(GUEST_IF),
                vec![BridgeEvent::destroyed()],
            ),
        ];
        "multiple interfaces"
    )]
    #[test_case(
        // Verify that even if all guests on a network are removed and the bridge is therefore
        // destroyed, if an interface is added again, the bridge is re-created with the same
        // upstream.
        [
            (Action::SetUpstream(UPSTREAM_IF), vec![]),
            (
                Action::AddInterface(GUEST_IF),
                vec![BridgeEvent::created([GUEST_IF].into(), UPSTREAM_IF)],
            ),
            (Action::RemoveInterface(GUEST_IF), vec![BridgeEvent::destroyed()]),
            (
                Action::AddInterface(GUEST_IF),
                vec![BridgeEvent::created([GUEST_IF].into(), UPSTREAM_IF)],
            ),
        ];
        "remember upstream"
    )]
    #[test_case(
        // Verify that the handler keeps track of which interfaces are added and removed to the
        // bridged network even when there is no existing bridge due to a lack of upstream
        // connectivity.
        [
            (Action::SetUpstream(UPSTREAM_IF), vec![]),
            (
                Action::AddInterface(GUEST_IF),
                vec![BridgeEvent::created([GUEST_IF].into(), UPSTREAM_IF)],
            ),
            (Action::RemoveUpstream, vec![BridgeEvent::destroyed()]),
            (Action::AddInterface(GUEST_IF2), vec![]),
            (Action::RemoveInterface(GUEST_IF), vec![]),
            (
                Action::SetUpstream(UPSTREAM_IF),
                vec![BridgeEvent::created([GUEST_IF2].into(), UPSTREAM_IF)],
            ),
        ];
        "remember guest interfaces"
    )]
    #[test_case(
        // Verify that when we replace the interface providing upstream connectivity with another
        // interface, we observe the same behavior as if it were removed and then added.
        [
            (Action::SetUpstream(UPSTREAM_IF), vec![]),
            (
                Action::AddInterface(GUEST_IF),
                vec![BridgeEvent::created([GUEST_IF].into(), UPSTREAM_IF)],
            ),
            (
                Action::SetUpstream(UPSTREAM_IF2),
                vec![
                    BridgeEvent::destroyed(),
                    BridgeEvent::created([GUEST_IF].into(), UPSTREAM_IF2),
                ],
            ),
        ];
        "replace upstream"
    )]
    #[test_case(
        // Verify that when we replace the interface providing upstream connectivity with another
        // interface, we observe the same behavior as if it were removed and then added.
        [
            (Action::SetUpstream(UPSTREAM_IF), vec![]),
            (Action::SetUpstream(UPSTREAM_IF2), vec![]),
            (
                Action::AddInterface(GUEST_IF),
                vec![BridgeEvent::created([GUEST_IF].into(), UPSTREAM_IF2)],
            ),
        ];
        "replace upstream with no guests"
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn bridge(steps: impl IntoIterator<Item = (Action, Vec<BridgeEvent>)>) {
        // At most 2 events will need to be sent before the test can process them: in the case that
        // a bridge is modified, the bridge is destroyed and then built again.
        let (events_tx, mut events_rx) = mpsc::channel(2);
        let (installer, _installer_server) =
            fidl::endpoints::create_proxy::<fnet_interfaces_admin::InstallerMarker>()
                .expect("create endpoints");
        let mut handler =
            Virtualization::new(HashSet::new(), BridgeHandlerTestImpl::new(events_tx), installer);

        for (action, expected_events) in steps {
            match action {
                Action::AddInterface(id) => {
                    handler.add_interface_to_bridge(id).await.expect("add interface to bridge");
                }
                Action::RemoveInterface(id) => {
                    handler
                        .remove_interface_from_bridge(id)
                        .await
                        .expect("remove interface from bridge");
                }
                Action::SetUpstream(id) => {
                    handler
                        .set_bridge_upstream_interface(id)
                        .await
                        .expect("set upstream interface");
                }
                Action::RemoveUpstream => {
                    handler
                        .remove_bridge_upstream_interface()
                        .await
                        .expect("remove upstream interface");
                }
            }
            for event in expected_events {
                assert_eq!(events_rx.next().await.expect("receive event"), event);
            }
            let _: mpsc::TryRecvError =
                events_rx.try_next().expect_err("got unexpected bridge event");
        }
    }
}
