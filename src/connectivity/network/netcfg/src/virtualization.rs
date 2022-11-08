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
use derivative::Derivative;
use futures::{channel::oneshot, future, FutureExt as _, StreamExt as _, TryStreamExt as _};
use tracing::{debug, error, info, warn};

use crate::{
    errors::{self, ContextExt as _},
    DeviceClass,
};

/// Network identifier.
#[derive(Debug, Clone)]
pub(super) enum NetworkId {
    // TODO(https://fxbug.dev/86068): Implement isolation between bridged
    // networks and change this to represent more than a single bridged
    // network.
    /// The unique bridged network consisting of an upstream-providing
    /// interface and all guest interfaces.
    Bridged,
}

/// Wrapper around a [`fnet_virtualization::NetworkRequest`] that allows for the
/// server to notify all existing interfaces to shut down when the client closes
/// the `Network` channel.
#[derive(Debug)]
pub(super) enum NetworkRequest {
    Request(fnet_virtualization::NetworkRequest, future::Shared<oneshot::Receiver<()>>),
    Finished(oneshot::Sender<()>),
}

#[derive(Derivative)]
#[derivative(Debug)]
pub(super) enum Event {
    ControlRequestStream(#[derivative(Debug = "ignore")] fnet_virtualization::ControlRequestStream),
    ControlRequest(fnet_virtualization::ControlRequest),
    NetworkRequest(NetworkId, NetworkRequest),
    InterfaceClose(NetworkId, u64),
}

pub(super) type EventStream = Pin<Box<dyn futures::stream::Stream<Item = Event>>>;

// TODO(https://fxbug.dev/88017): `guests` must always be non-empty. Explore using a non-empty set
// type to encode this invariant in the type system.
enum BridgeState {
    Init,
    WaitingForGuests {
        // Invariants: `upstream` is not present in `upstream_candidates`, and
        // must be online.
        upstream: u64,
        upstream_candidates: HashSet<u64>,
    },
    WaitingForUpstream {
        guests: HashSet<u64>,
    },
    Bridged {
        bridge_id: u64,
        // Invariant: `upstream` is not present in `upstream_candidates`.
        //
        // Note that `upstream` going offline does not cause a state transition unlike
        // `WaitingForGuests`, and thus `upstream` may be offline.
        upstream: u64,
        upstream_candidates: HashSet<u64>,
        guests: HashSet<u64>,
    },
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

// TODO(https://github.com/rust-lang/rust/issues/59618): Implement using
// `drain_filter` to avoid the Copy trait bound.
// Takes a single element from `set` if `set` is non-empty.
fn take_any<T: std::marker::Copy + std::cmp::Eq + std::hash::Hash>(
    set: &mut HashSet<T>,
) -> Option<T> {
    set.iter().copied().next().map(|elem| {
        assert!(set.remove(&elem));
        elem
    })
}

pub(super) struct Virtualization<B: BridgeHandler> {
    installer: fnet_interfaces_admin::InstallerProxy,
    // TODO(https://fxbug.dev/101224): Use this field as the allowed upstream
    // device classes when NAT is supported.
    _allowed_upstream_device_classes: HashSet<DeviceClass>,
    allowed_bridge_upstream_device_classes: HashSet<DeviceClass>,
    bridge_handler: B,
    bridge_state: BridgeState,
}

impl<B: BridgeHandler> Virtualization<B> {
    pub fn new(
        _allowed_upstream_device_classes: HashSet<DeviceClass>,
        allowed_bridge_upstream_device_classes: HashSet<DeviceClass>,
        bridge_handler: B,
        installer: fnet_interfaces_admin::InstallerProxy,
    ) -> Self {
        Self {
            installer,
            _allowed_upstream_device_classes,
            allowed_bridge_upstream_device_classes,
            bridge_handler,
            bridge_state: Default::default(),
        }
    }

    fn is_device_class_allowed_for_bridge_upstream(
        &self,
        device_class: fnet_interfaces::DeviceClass,
    ) -> bool {
        match device_class {
            fnet_interfaces::DeviceClass::Device(device_class) => {
                self.allowed_bridge_upstream_device_classes.contains(&device_class.into())
            }
            fnet_interfaces::DeviceClass::Loopback(fnet_interfaces::Empty {}) => false,
        }
    }

    async fn handle_network_request(
        &mut self,
        network_id: NetworkId,
        request: NetworkRequest,
        events: &mut futures::stream::SelectAll<EventStream>,
    ) -> Result<(), errors::Error> {
        match request {
            NetworkRequest::Request(
                fnet_virtualization::NetworkRequest::AddPort { port, interface, control_handle: _ },
                mut network_close_rx,
            ) => {
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

                let (device_control, server_end) =
                    fidl::endpoints::create_proxy::<fnet_interfaces_admin::DeviceControlMarker>()
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
                let (control, server_end) = fnet_interfaces_ext::admin::Control::create_endpoints()
                    .context("create Control endpoints")
                    .map_err(errors::Error::NonFatal)?;
                device_control
                    .create_interface(
                        &mut port_id,
                        server_end,
                        fnet_interfaces_admin::Options { ..fnet_interfaces_admin::Options::EMPTY },
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

                match network_id {
                    NetworkId::Bridged => {
                        // Add this interface to the existing bridge, or create one if none exists.
                        self.add_guest_to_bridge(id).await.context("adding interface to bridge")?;
                    }
                }

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
                            request.map(|_request: fnet_virtualization::InterfaceRequest| ())
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
                    futures::stream::once(
                        shutdown_fut.map(|id| Event::InterfaceClose(network_id, id)),
                    )
                    .boxed(),
                );
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

    async fn add_guest_to_bridge(&mut self, id: u64) -> Result<(), errors::Error> {
        info!("got a request to add interface {} to bridge", id);
        let Self { bridge_state, bridge_handler, .. } = self;
        *bridge_state = match std::mem::take(bridge_state) {
            BridgeState::Init => BridgeState::WaitingForUpstream { guests: HashSet::from([id]) },
            // If a bridge doesn't exist, but we have an interface with upstream connectivity,
            // create the bridge.
            BridgeState::WaitingForGuests { upstream, upstream_candidates } => {
                let guests = HashSet::from([id]);
                let bridge_id = bridge_handler
                    .build_bridge(guests.iter().copied(), upstream)
                    .await
                    .context("building bridge")?;
                BridgeState::Bridged { bridge_id, upstream, upstream_candidates, guests }
            }
            // If a bridge doesn't exist, and we don't yet have an interface with upstream
            // connectivity, just keep track of the interface to be bridged, so we can eventually
            // include it in the bridge.
            BridgeState::WaitingForUpstream { mut guests } => {
                assert!(guests.insert(id));
                // No change to bridge state.
                BridgeState::WaitingForUpstream { guests }
            }
            // If a bridge already exists, tear it down and create a new one, re-using the interface
            // that has upstream connectivity and including all the interfaces that were bridged
            // previously.
            BridgeState::Bridged { bridge_id, upstream, upstream_candidates, mut guests } => {
                bridge_handler.destroy_bridge(bridge_id).await.context("destroying bridge")?;
                assert!(guests.insert(id));
                let bridge_id = bridge_handler
                    .build_bridge(guests.iter().copied(), upstream)
                    .await
                    .context("building bridge")?;
                BridgeState::Bridged { bridge_id, upstream, upstream_candidates, guests }
            }
        };
        Ok(())
    }

    async fn remove_guest_from_bridge(&mut self, id: u64) -> Result<(), errors::Error> {
        info!("got a request to remove interface {} from bridge", id);
        let Self { bridge_state, bridge_handler, .. } = self;
        *bridge_state = match std::mem::take(bridge_state) {
            BridgeState::Init | BridgeState::WaitingForGuests { .. } => {
                panic!("cannot remove guest interface {} since it was not previously added", id)
            }
            BridgeState::WaitingForUpstream { mut guests } => {
                assert!(guests.remove(&id));
                if guests.is_empty() {
                    BridgeState::Init
                } else {
                    // No change to bridge state.
                    BridgeState::WaitingForUpstream { guests }
                }
            }
            BridgeState::Bridged { bridge_id, upstream, upstream_candidates, mut guests } => {
                bridge_handler.destroy_bridge(bridge_id).await.context("destroying bridge")?;
                assert!(guests.remove(&id));
                if guests.is_empty() {
                    BridgeState::WaitingForGuests { upstream, upstream_candidates }
                } else {
                    let bridge_id = self
                        .bridge_handler
                        .build_bridge(guests.iter().copied(), upstream)
                        .await
                        .context("building bridge")?;
                    BridgeState::Bridged { bridge_id, upstream, upstream_candidates, guests }
                }
            }
        };
        Ok(())
    }

    async fn handle_interface_online(
        &mut self,
        id: u64,
        allowed_for_bridge_upstream: bool,
    ) -> Result<(), errors::Error> {
        info!("interface {} (allowed for upstream: {}) is online", id, allowed_for_bridge_upstream);
        let Self { bridge_state, bridge_handler, .. } = self;
        *bridge_state = match std::mem::take(bridge_state) {
            BridgeState::Init => {
                if allowed_for_bridge_upstream {
                    BridgeState::WaitingForGuests {
                        upstream: id,
                        upstream_candidates: Default::default(),
                    }
                } else {
                    BridgeState::Init
                }
            }
            BridgeState::WaitingForGuests { upstream, mut upstream_candidates } => {
                if allowed_for_bridge_upstream {
                    assert_ne!(
                        upstream, id,
                        "interface {} expected to provide upstream but was offline and came online",
                        id
                    );
                    assert!(
                        upstream_candidates.insert(id),
                        "upstream candidate {} already present",
                        id
                    );
                }
                BridgeState::WaitingForGuests { upstream, upstream_candidates }
            }
            BridgeState::WaitingForUpstream { guests } => {
                if allowed_for_bridge_upstream && !guests.contains(&id) {
                    // We don't already have an upstream interface that provides connectivity. Build
                    // a bridge with this one.
                    let bridge_id = bridge_handler
                        .build_bridge(guests.iter().copied(), id)
                        .await
                        .context("building bridge")?;
                    BridgeState::Bridged {
                        bridge_id,
                        upstream: id,
                        upstream_candidates: Default::default(),
                        guests,
                    }
                } else {
                    BridgeState::WaitingForUpstream { guests }
                }
            }
            // If a bridge already exists, tear it down and create a new one, using this new
            // interface to provide upstream connectivity, and including all the interfaces that
            // were bridged previously.
            BridgeState::Bridged { bridge_id, upstream, mut upstream_candidates, guests } => {
                if id == upstream {
                    info!("upstream-providing interface {} went online", id);
                } else if id == bridge_id {
                    info!("bridge interface {} went online", bridge_id);
                } else if !guests.contains(&id) && allowed_for_bridge_upstream {
                    assert!(
                        upstream_candidates.insert(id),
                        "upstream candidate {} already present",
                        id
                    );
                }
                BridgeState::Bridged { bridge_id, upstream, upstream_candidates, guests }
            }
        };
        Ok(())
    }

    async fn handle_interface_offline(
        &mut self,
        id: u64,
        allowed_for_bridge_upstream: bool,
    ) -> Result<(), errors::Error> {
        info!(
            "interface {} (allowed for upstream: {}) is offline",
            id, allowed_for_bridge_upstream
        );
        let Self { bridge_state, .. } = self;
        *bridge_state = match std::mem::take(bridge_state) {
            BridgeState::Init => BridgeState::Init,
            BridgeState::WaitingForUpstream { guests } => {
                BridgeState::WaitingForUpstream { guests }
            }
            BridgeState::Bridged { bridge_id, upstream, mut upstream_candidates, guests } => {
                if id == bridge_id {
                    warn!("bridge interface {} went offline", id);
                } else if id == upstream {
                    // We currently ignore the situation where an interface that is providing
                    // upstream connectivity changes from online to offline. The only signal
                    // that causes us to destroy an existing bridge is if the interface providing
                    // upstream connectivity is removed entirely.
                    warn!("upstream interface {} went offline", id);
                } else if !guests.contains(&id) && allowed_for_bridge_upstream {
                    assert!(upstream_candidates.remove(&id), "upstream candidate {} not found", id);
                }
                BridgeState::Bridged { bridge_id, upstream, upstream_candidates, guests }
            }
            BridgeState::WaitingForGuests { upstream, mut upstream_candidates } => {
                if id == upstream {
                    match take_any(&mut upstream_candidates) {
                        Some(id) => {
                            BridgeState::WaitingForGuests { upstream: id, upstream_candidates }
                        }
                        None => BridgeState::Init,
                    }
                } else {
                    if allowed_for_bridge_upstream {
                        assert!(
                            upstream_candidates.remove(&id),
                            "upstream candidate {} not found",
                            id
                        );
                    }
                    BridgeState::WaitingForGuests { upstream, upstream_candidates }
                }
            }
        };
        Ok(())
    }

    async fn handle_interface_removed(&mut self, removed_id: u64) -> Result<(), errors::Error> {
        info!("interface {} removed", removed_id);
        let Self { bridge_state, bridge_handler, .. } = self;
        *bridge_state = match std::mem::take(bridge_state) {
            BridgeState::Init => BridgeState::Init,
            BridgeState::WaitingForUpstream { guests } => {
                if guests.contains(&removed_id) {
                    // Removal from the `guests` map will occur when the guest removal is
                    // actually handled in `remove_guest_from_bridge`.
                    info!("guest interface {} removed", removed_id);
                }
                BridgeState::WaitingForUpstream { guests }
            }
            BridgeState::WaitingForGuests { upstream, mut upstream_candidates } => {
                if upstream == removed_id {
                    match take_any(&mut upstream_candidates) {
                        Some(new_upstream_id) => BridgeState::WaitingForGuests {
                            upstream: new_upstream_id,
                            upstream_candidates,
                        },
                        None => BridgeState::Init,
                    }
                } else {
                    let _: bool = upstream_candidates.remove(&removed_id);
                    BridgeState::WaitingForGuests { upstream, upstream_candidates }
                }
            }
            BridgeState::Bridged { bridge_id, upstream, mut upstream_candidates, guests } => {
                if guests.contains(&removed_id) {
                    // Removal from the `guests` map will occur when the guest removal is
                    // actually handled in `remove_guest_from_bridge`.
                    info!("guest interface {} removed", removed_id);
                }
                if bridge_id == removed_id {
                    // The bridge interface installed by netcfg should not be removed by any other
                    // entity.
                    error!("bridge interface {} removed; rebuilding", bridge_id);
                    let bridge_id = self
                        .bridge_handler
                        .build_bridge(guests.iter().copied(), upstream)
                        .await
                        .context("building bridge")?;
                    BridgeState::Bridged { bridge_id, upstream, upstream_candidates, guests }
                } else if upstream == removed_id {
                    bridge_handler.destroy_bridge(bridge_id).await.context("destroying bridge")?;
                    match take_any(&mut upstream_candidates) {
                        Some(new_upstream_id) => {
                            let bridge_id = self
                                .bridge_handler
                                .build_bridge(guests.iter().copied(), new_upstream_id)
                                .await
                                .context("building bridge")?;
                            BridgeState::Bridged {
                                bridge_id,
                                upstream: new_upstream_id,
                                upstream_candidates,
                                guests,
                            }
                        }
                        None => BridgeState::WaitingForUpstream { guests },
                    }
                } else {
                    let _: bool = upstream_candidates.remove(&removed_id);
                    BridgeState::Bridged { bridge_id, upstream, upstream_candidates, guests }
                }
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
            Event::ControlRequest(fnet_virtualization::ControlRequest::CreateNetwork {
                config,
                network,
                control_handle: _,
            }) => {
                let network_id = match config {
                    fnet_virtualization::Config::Bridged(fnet_virtualization::Bridged {
                        ..
                    }) => {
                        info!("got a request to create a bridged network");
                        NetworkId::Bridged
                    }
                    config => {
                        panic!("unsupported network config type {:?}", config);
                    }
                };
                // Create a oneshot channel we can use when the `Network` channel is closed,
                // to notify each interface task to close its corresponding `Interface` channel
                // as well.
                let (close_channel_tx, close_channel_rx) = oneshot::channel();
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
                events.push(
                    stream.map(move |r| Event::NetworkRequest(network_id.clone(), r)).boxed(),
                );
            }
            Event::NetworkRequest(network_id, request) => self
                .handle_network_request(network_id, request, events)
                .await
                .context("handle network request")?,
            Event::InterfaceClose(network_id, id) => {
                match network_id {
                    NetworkId::Bridged => {
                        // Remove this interface from the existing bridge.
                        self.remove_guest_from_bridge(id)
                            .await
                            .context("removing interface from bridge")?;
                    }
                }
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
                let allowed_for_bridge_upstream =
                    self.is_device_class_allowed_for_bridge_upstream(device_class);

                if online {
                    self.handle_interface_online(id, allowed_for_bridge_upstream)
                        .await
                        .context("handle new interface online")?;
                }
            }
            fnet_interfaces_ext::UpdateResult::Changed {
                previous: fnet_interfaces::Properties { online: previously_online, .. },
                current: current_properties,
            } => {
                let fnet_interfaces_ext::Properties { id, online, device_class, .. } =
                    **current_properties;
                let allowed_for_bridge_upstream =
                    self.is_device_class_allowed_for_bridge_upstream(device_class);

                match (*previously_online, online) {
                    (Some(false), true) => {
                        self.handle_interface_online(id, allowed_for_bridge_upstream)
                            .await
                            .context("handle interface online")?;
                    }
                    (Some(true), false) => {
                        self.handle_interface_offline(id, allowed_for_bridge_upstream)
                            .await
                            .context("handle interface offline")?;
                    }
                    (Some(true), true) | (Some(false), false) => {
                        error!("interface {} changed event indicates no actual change to online ({} before and after)", id, online);
                    }
                    // Online did not change; do nothing.
                    (None, true) => {}
                    (None, false) => {}
                }
            }
            fnet_interfaces_ext::UpdateResult::Removed(fnet_interfaces_ext::Properties {
                id,
                ..
            }) => {
                self.handle_interface_removed(*id).await.context("handle interface removed")?;
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

    // Starts a DHCPv4 client.
    async fn start_dhcpv4_client(&self, bridge_id: u64) -> Result<(), errors::Error> {
        self.stack
            .set_dhcp_client_enabled(bridge_id, true)
            .await
            .context("failed to call SetDhcpClientEnabled")
            .map_err(errors::Error::Fatal)?
            .map_err(|e| anyhow!("failed to start dhcp client: {:?}", e))
            .map_err(errors::Error::NonFatal)
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

            info!(
                "building bridge with upstream={}, interfaces={:?}",
                upstream_interface, interfaces
            );

            self.netstack
                .bridge_interfaces(&interfaces)
                .await
                .map_err(anyhow::Error::new)
                .and_then(|result| match result {
                    fnetstack::Result_::Message(message) => Err(anyhow::Error::msg(message)),
                    fnetstack::Result_::Nicid(id) => Ok(id),
                })
                .with_context(|| format!("could not bridge interfaces ({:?})", interfaces))
                .map_err(errors::Error::Fatal)?
        };

        let bridge_id = u64::from(bridge_id);

        // Start a DHCPv4 client.
        match self.start_dhcpv4_client(bridge_id).await {
            Ok(()) => {}
            Err(errors::Error::NonFatal(e)) => {
                error!("failed to start DHCPv4 client on bridge: {}", e)
            }
            Err(errors::Error::Fatal(e)) => return Err(errors::Error::Fatal(e)),
        }

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

    #[derive(Copy, Clone, Debug, PartialEq)]
    enum Guest {
        A,
        B,
    }

    impl Guest {
        fn id(&self) -> u64 {
            match self {
                Self::A => 1,
                Self::B => 2,
            }
        }
    }

    #[derive(Copy, Clone, Debug, PartialEq)]
    enum Upstream {
        A,
        B,
    }

    impl Upstream {
        fn id(&self) -> u64 {
            match self {
                Self::A => 11,
                Self::B => 12,
            }
        }
    }

    #[derive(Debug, PartialEq)]
    enum BridgeEvent {
        Destroyed,
        Created { interfaces: HashSet<u64>, upstream_interface: u64 },
    }

    impl BridgeEvent {
        fn created(interfaces: Vec<Guest>, upstream_interface: Upstream) -> Self {
            Self::Created {
                interfaces: interfaces.iter().map(Guest::id).collect(),
                upstream_interface: upstream_interface.id(),
            }
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

    enum Action {
        AddGuest(Guest),
        RemoveGuest(Guest),
        UpstreamOnline(Upstream),
        UpstreamOffline(Upstream),
        RemoveUpstream(Upstream),
    }

    #[test_case(
        // Verify that we wait to create a bridge until an interface is added to a virtual bridged
        // network.
        [
            (Action::UpstreamOnline(Upstream::A), vec![]),
            (
                Action::AddGuest(Guest::A),
                vec![BridgeEvent::created([Guest::A].into(), Upstream::A)],
            ),
        ];
        "wait for guest"
    )]
    #[test_case(
        // Verify that we wait to create a bridge until there is an interface that provides upstream
        // connectivity.
        [
            (Action::AddGuest(Guest::A), vec![]),
            (
                Action::UpstreamOnline(Upstream::A),
                vec![BridgeEvent::created([Guest::A].into(), Upstream::A)],
            ),
        ];
        "wait for upstream"
    )]
    #[test_case(
        // Verify that the bridge is destroyed when the upstream interface is removed and there are
        // no more candidates to provide upstream connectivity.
        [
            (Action::UpstreamOnline(Upstream::A), vec![]),
            (
                Action::AddGuest(Guest::A),
                vec![BridgeEvent::created([Guest::A].into(), Upstream::A)],
            ),
            (Action::RemoveUpstream(Upstream::A), vec![BridgeEvent::destroyed()]),
        ];
        "destroy bridge when no upstream"
    )]
    #[test_case(
        // Verify that when we add multiple interfaces to the virtual network, they are added to the
        // bridge one by one, which is implemented by tearing down the bridge and rebuilding it
        // every time an interface is added or removed.
        [
            (Action::UpstreamOnline(Upstream::A), vec![]),
            (
                Action::AddGuest(Guest::A),
                vec![BridgeEvent::created([Guest::A].into(), Upstream::A)],
            ),
            (
                Action::AddGuest(Guest::B),
                vec![
                    BridgeEvent::destroyed(),
                    BridgeEvent::created([Guest::A, Guest::B].into(), Upstream::A),
                ],
            ),
            (
                Action::RemoveGuest(Guest::B),
                vec![
                    BridgeEvent::destroyed(),
                    BridgeEvent::created([Guest::A].into(), Upstream::A),
                ],
            ),
            (
                Action::RemoveGuest(Guest::A),
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
            (Action::UpstreamOnline(Upstream::A), vec![]),
            (
                Action::AddGuest(Guest::A),
                vec![BridgeEvent::created([Guest::A].into(), Upstream::A)],
            ),
            (Action::RemoveGuest(Guest::A), vec![BridgeEvent::destroyed()]),
            (
                Action::AddGuest(Guest::A),
                vec![BridgeEvent::created([Guest::A].into(), Upstream::A)],
            ),
        ];
        "remember upstream"
    )]
    #[test_case(
        // Verify that the handler keeps track of which interfaces are added and removed to the
        // bridged network even when there is no existing bridge due to a lack of upstream
        // connectivity.
        [
            (Action::UpstreamOnline(Upstream::A), vec![]),
            (
                Action::AddGuest(Guest::A),
                vec![BridgeEvent::created([Guest::A].into(), Upstream::A)],
            ),
            (Action::RemoveUpstream(Upstream::A), vec![BridgeEvent::destroyed()]),
            (Action::AddGuest(Guest::B), vec![]),
            (Action::RemoveGuest(Guest::A), vec![]),
            (
                Action::UpstreamOnline(Upstream::A),
                vec![BridgeEvent::created([Guest::B].into(), Upstream::A)],
            ),
        ];
        "remember guest interfaces"
    )]
    #[test_case(
        // Verify that the bridge is destroyed when upstream is removed.
        [
            (Action::UpstreamOnline(Upstream::A), vec![]),
            (
                Action::AddGuest(Guest::A),
                vec![BridgeEvent::created([Guest::A].into(), Upstream::A)],
            ),
            (Action::RemoveUpstream(Upstream::A), vec![BridgeEvent::destroyed()]),
        ];
        "remove upstream"
    )]
    #[test_case(
        // Verify that the upstream-providing interface going offline while a
        // bridge is present does not cause the bridge to be destroyed.
        [
            (Action::UpstreamOnline(Upstream::A), vec![]),
            (
                Action::AddGuest(Guest::A),
                vec![BridgeEvent::created([Guest::A].into(), Upstream::A)],
            ),
            (Action::UpstreamOffline(Upstream::A), vec![]),
        ];
        "upstream offline not removed"
    )]
    #[test_case(
        // Verify that an otherwise eligible but offline upstream interface is
        // not used to create a bridge.
        [
            (Action::UpstreamOnline(Upstream::A), vec![]),
            (Action::UpstreamOffline(Upstream::A), vec![]),
            (Action::AddGuest(Guest::A), vec![]),
            (
                Action::UpstreamOnline(Upstream::A),
                vec![BridgeEvent::created([Guest::A].into(), Upstream::A)],
            ),
        ];
        "do not bridge with offline upstream"
    )]
    #[test_case(
        // Verify that when we replace the interface providing upstream connectivity with another
        // interface, the bridge is correctly destroyed and recreated with the new upstream.
        [
            (Action::UpstreamOnline(Upstream::A), vec![]),
            (
                Action::AddGuest(Guest::A),
                vec![BridgeEvent::created([Guest::A].into(), Upstream::A)],
            ),
            (Action::UpstreamOnline(Upstream::B), vec![]),
            (
                Action::RemoveUpstream(Upstream::A),
                vec![
                    BridgeEvent::destroyed(),
                    BridgeEvent::created([Guest::A].into(), Upstream::B),
                ],
            ),
        ];
        "replace upstream"
    )]
    #[test_case(
        // Verify that upstream-providing interface changes are tracked even when there are no
        // guests yet.
        [
            (Action::UpstreamOnline(Upstream::A), vec![]),
            (Action::UpstreamOnline(Upstream::B), vec![]),
            (Action::UpstreamOffline(Upstream::A), vec![]),
            (
                Action::AddGuest(Guest::A),
                vec![BridgeEvent::created([Guest::A].into(), Upstream::B)],
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
        let mut handler = Virtualization::new(
            HashSet::new(),
            HashSet::new(),
            BridgeHandlerTestImpl::new(events_tx),
            installer,
        );

        for (action, expected_events) in steps {
            match action {
                Action::AddGuest(guest) => {
                    handler.add_guest_to_bridge(guest.id()).await.expect("add guest to bridge");
                }
                Action::RemoveGuest(guest) => {
                    handler
                        .remove_guest_from_bridge(guest.id())
                        .await
                        .expect("remove guest from bridge");
                }
                Action::UpstreamOnline(upstream) => {
                    handler
                        .handle_interface_online(upstream.id(), true)
                        .await
                        .expect("upstream interface online");
                }
                Action::UpstreamOffline(upstream) => {
                    handler
                        .handle_interface_offline(upstream.id(), true)
                        .await
                        .expect("upstream interface offline");
                }
                Action::RemoveUpstream(upstream) => {
                    handler
                        .handle_interface_removed(upstream.id())
                        .await
                        .expect("upstream interface removed");
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
