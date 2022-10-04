// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! FIDL Worker for the `fuchsia.net.interfaces.admin` API, including the
//! `DeviceControl`, `Control` and `AddressStateProvider` Protocols.
//!
//! Each instance of these protocols is tied to the lifetime of a particular
//! entity in the Netstack:
//!    `DeviceControl`        => device
//!    `Control`              => interface
//!    `AddressStateProvider` => address
//! meaning the entity is removed if the protocol is closed, and the protocol is
//! closed if the entity is removed. Some protocols expose a `detach` method
//! that allows the lifetimes to be decoupled.
//!
//! These protocols (and their corresponding entities) are nested:
//! `DeviceControl` allows a new `Control` protocol to be connected (creating a
//! new interface on the device), while `Control` allows a new
//! `AddressStateProvider` protocol to be connected (creating a new address on
//! the interface).
//!
//! In general, each protocol is served by a "worker", either a
//! [`fuchsia_async::Task`] or a bare [`futures::future::Future`], that handles
//! incoming requests, spawns the workers for the nested protocols, and tears
//! down its associated entity if canceled.
//!
//! The `fuchsia.net.debug/Interfaces.GetAdmin` method exposes a backdoor that
//! allows clients to deviate from the behavior described above. Clients can
//! attach a new `Control` protocol to an existing interface with one-way
//! ownership semantics (removing the interface closes the protocol; closing
//! protocol does not remove the interface).

use std::collections::hash_map;
use std::ops::DerefMut as _;

use assert_matches::assert_matches;
use fidl::endpoints::{ProtocolMarker, ServerEnd};
use fidl_fuchsia_hardware_network as fhardware_network;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{
    future::FusedFuture as _, stream::FusedStream as _, FutureExt as _, SinkExt as _,
    StreamExt as _, TryFutureExt as _, TryStreamExt as _,
};
use net_types::{ip::AddrSubnetEither, ip::IpAddr, SpecifiedAddr};
use netstack3_core::Ctx;

use crate::bindings::{
    devices, netdevice_worker, util, util::IntoCore as _, util::TryIntoCore as _, BindingId,
    InterfaceControl as _, Netstack, NetstackContext,
};

pub(crate) fn serve(
    ns: Netstack,
    req: fnet_interfaces_admin::InstallerRequestStream,
) -> impl futures::Stream<Item = Result<fasync::Task<()>, fidl::Error>> {
    req.map_ok(
        move |fnet_interfaces_admin::InstallerRequest::InstallDevice {
                  device,
                  device_control,
                  control_handle: _,
              }| {
            fasync::Task::spawn(
                run_device_control(
                    ns.clone(),
                    device,
                    device_control.into_stream().expect("failed to obtain stream"),
                )
                .map(|r| r.unwrap_or_else(|e| log::warn!("device control finished with {:?}", e))),
            )
        },
    )
}

#[derive(thiserror::Error, Debug)]
enum DeviceControlError {
    #[error("worker error: {0}")]
    Worker(#[from] netdevice_worker::Error),
    #[error("fidl error: {0}")]
    Fidl(#[from] fidl::Error),
}

async fn run_device_control(
    ns: Netstack,
    device: fidl::endpoints::ClientEnd<fhardware_network::DeviceMarker>,
    req_stream: fnet_interfaces_admin::DeviceControlRequestStream,
) -> Result<(), DeviceControlError> {
    let worker = netdevice_worker::NetdeviceWorker::new(ns.ctx.clone(), device).await?;
    let handler = worker.new_handler();
    let worker_fut = worker.run().map_err(DeviceControlError::Worker);
    let stop_event = async_utils::event::Event::new();
    let req_stream =
        req_stream.take_until(stop_event.wait_or_dropped()).map_err(DeviceControlError::Fidl);
    futures::pin_mut!(worker_fut);
    futures::pin_mut!(req_stream);
    let mut detached = false;
    let mut tasks = futures::stream::FuturesUnordered::new();
    let res = loop {
        let result = futures::select! {
            req = req_stream.try_next() => req,
            r = worker_fut => match r {
                Ok(never) => match never {},
                Err(e) => Err(e)
            },
            ready_task = tasks.next() => {
                let () = ready_task.unwrap_or_else(|| ());
                continue;
            }
        };
        match result {
            Ok(None) => {
                // The client hung up; stop serving if not detached.
                if !detached {
                    break Ok(());
                }
            }
            Ok(Some(req)) => match req {
                fnet_interfaces_admin::DeviceControlRequest::CreateInterface {
                    port,
                    control,
                    options,
                    control_handle: _,
                } => {
                    if let Some(interface_control_task) = create_interface(
                        port,
                        control,
                        options,
                        &ns,
                        &handler,
                        stop_event.wait_or_dropped(),
                    )
                    .await
                    {
                        tasks.push(interface_control_task);
                    }
                }
                fnet_interfaces_admin::DeviceControlRequest::Detach { control_handle: _ } => {
                    detached = true;
                }
            },
            Err(e) => break Err(e),
        }
    };

    // Send a stop signal to all tasks.
    assert!(stop_event.signal(), "event was already signaled");
    // Run all the tasks to completion. We sent the stop signal, they should all
    // complete and perform interface cleanup.
    tasks.collect::<()>().await;

    res
}

const INTERFACES_ADMIN_CHANNEL_SIZE: usize = 16;
/// A wrapper over `fuchsia.net.interfaces.admin/Control` handles to express ownership semantics.
///
/// If `owns_interface` is true, this handle 'owns' the interfaces, and when the handle closes the
/// interface should be removed.
pub struct OwnedControlHandle {
    request_stream: fnet_interfaces_admin::ControlRequestStream,
    control_handle: fnet_interfaces_admin::ControlControlHandle,
    owns_interface: bool,
}

impl OwnedControlHandle {
    pub(crate) fn new_unowned(
        handle: fidl::endpoints::ServerEnd<fnet_interfaces_admin::ControlMarker>,
    ) -> OwnedControlHandle {
        let (stream, control) =
            handle.into_stream_and_control_handle().expect("failed to decompose control handle");
        OwnedControlHandle {
            request_stream: stream,
            control_handle: control,
            owns_interface: false,
        }
    }

    // Constructs a new channel of `OwnedControlHandle` with no owner.
    pub(crate) fn new_channel() -> (
        futures::channel::mpsc::Sender<OwnedControlHandle>,
        futures::channel::mpsc::Receiver<OwnedControlHandle>,
    ) {
        futures::channel::mpsc::channel(INTERFACES_ADMIN_CHANNEL_SIZE)
    }

    // Constructs a new channel of `OwnedControlHandle` with the given handle as the owner.
    // TODO(https://fxbug.dev/87963): This currently enforces that there is only ever one owner,
    // which will need to be revisited to implement `Clone`.
    pub(crate) async fn new_channel_with_owned_handle(
        handle: fidl::endpoints::ServerEnd<fnet_interfaces_admin::ControlMarker>,
    ) -> (
        futures::channel::mpsc::Sender<OwnedControlHandle>,
        futures::channel::mpsc::Receiver<OwnedControlHandle>,
    ) {
        let (mut sender, receiver) = Self::new_channel();
        let (stream, control) =
            handle.into_stream_and_control_handle().expect("failed to decompose control handle");
        sender
            .send(OwnedControlHandle {
                request_stream: stream,
                control_handle: control,
                owns_interface: true,
            })
            .await
            .expect("failed to attach initial control handle");
        (sender, receiver)
    }

    // Consumes the OwnedControlHandle and returns its `control_handle`.
    pub(crate) fn into_control_handle(self) -> fnet_interfaces_admin::ControlControlHandle {
        self.control_handle
    }
}

/// Operates a fuchsia.net.interfaces.admin/DeviceControl.CreateInterface
/// request.
///
/// Returns `Some(fuchsia_async::Task)` if an interface was created
/// successfully. The returned `Task` must be polled to completion and is tied
/// to the created interface's lifetime.
async fn create_interface(
    port: fhardware_network::PortId,
    control: fidl::endpoints::ServerEnd<fnet_interfaces_admin::ControlMarker>,
    options: fnet_interfaces_admin::Options,
    ns: &Netstack,
    handler: &netdevice_worker::DeviceHandler,
    device_stopped_fut: async_utils::event::EventWaitResult,
) -> Option<fuchsia_async::Task<()>> {
    log::debug!("creating interface from {:?} with {:?}", port, options);
    let fnet_interfaces_admin::Options { name, metric: _, .. } = options;
    let (control_sender, mut control_receiver) =
        OwnedControlHandle::new_channel_with_owned_handle(control).await;
    match handler
        .add_port(&ns, netdevice_worker::InterfaceOptions { name }, port, control_sender)
        .await
    {
        Ok((binding_id, status_stream)) => {
            Some(fasync::Task::spawn(run_netdevice_interface_control(
                ns.ctx.clone(),
                binding_id,
                status_stream,
                device_stopped_fut,
                control_receiver,
            )))
        }
        Err(e) => {
            log::warn!("failed to add port {:?} to device: {:?}", port, e);
            let removed_reason = match e {
                netdevice_worker::Error::Client(e) => match e {
                    // Assume any fidl errors are port closed
                    // errors.
                    netdevice_client::Error::Fidl(_) => {
                        Some(fnet_interfaces_admin::InterfaceRemovedReason::PortClosed)
                    }
                    netdevice_client::Error::RxFlags(_)
                    | netdevice_client::Error::FrameType(_)
                    | netdevice_client::Error::NoProgress
                    | netdevice_client::Error::Config(_)
                    | netdevice_client::Error::LargeChain(_)
                    | netdevice_client::Error::Index(_, _)
                    | netdevice_client::Error::Pad(_, _)
                    | netdevice_client::Error::TxLength(_, _)
                    | netdevice_client::Error::Open(_, _)
                    | netdevice_client::Error::Vmo(_, _)
                    | netdevice_client::Error::Fifo(_, _, _)
                    | netdevice_client::Error::VmoSize(_, _)
                    | netdevice_client::Error::Map(_, _)
                    | netdevice_client::Error::DeviceInfo(_)
                    | netdevice_client::Error::PortStatus(_)
                    | netdevice_client::Error::InvalidPortId(_)
                    | netdevice_client::Error::Attach(_, _)
                    | netdevice_client::Error::Detach(_, _) => None,
                },
                netdevice_worker::Error::AlreadyInstalled(_) => {
                    Some(fnet_interfaces_admin::InterfaceRemovedReason::PortAlreadyBound)
                }
                netdevice_worker::Error::CantConnectToPort(_)
                | netdevice_worker::Error::PortClosed => {
                    Some(fnet_interfaces_admin::InterfaceRemovedReason::PortClosed)
                }
                netdevice_worker::Error::ConfigurationNotSupported
                | netdevice_worker::Error::MacNotUnicast { .. } => {
                    Some(fnet_interfaces_admin::InterfaceRemovedReason::BadPort)
                }
                netdevice_worker::Error::DuplicateName(_) => {
                    Some(fnet_interfaces_admin::InterfaceRemovedReason::DuplicateName)
                }
                netdevice_worker::Error::SystemResource(_)
                | netdevice_worker::Error::InvalidPortInfo(_) => None,
            };
            if let Some(removed_reason) = removed_reason {
                // Retrieve the original control handle from the receiver.
                let OwnedControlHandle { request_stream: _, control_handle, owns_interface: _ } =
                    control_receiver
                        .try_next()
                        .expect("expected control handle to be ready in the receiver")
                        .expect("expected receiver to not be closed/empty");
                control_handle.send_on_interface_removed(removed_reason).unwrap_or_else(|e| {
                    log::warn!("failed to send removed reason: {:?}", e);
                });
            }
            None
        }
    }
}

/// Manages the lifetime of a newly created Netdevice interface, including spawning an
/// interface control worker, spawning a link state worker, and cleaning up the interface on
/// deletion.
async fn run_netdevice_interface_control<
    S: futures::Stream<Item = netdevice_client::Result<netdevice_client::client::PortStatus>>,
>(
    ctx: NetstackContext,
    id: BindingId,
    status_stream: S,
    mut device_stopped_fut: async_utils::event::EventWaitResult,
    control_receiver: futures::channel::mpsc::Receiver<OwnedControlHandle>,
) {
    let link_state_fut = run_link_state_watcher(ctx.clone(), id, status_stream).fuse();
    let (interface_control_stop_sender, interface_control_stop_receiver) =
        futures::channel::oneshot::channel();
    let interface_control_fut =
        run_interface_control(ctx.clone(), id, interface_control_stop_receiver, control_receiver)
            .fuse();
    futures::pin_mut!(link_state_fut);
    futures::pin_mut!(interface_control_fut);
    futures::select! {
        o = device_stopped_fut => o.expect("event was orphaned"),
        () = link_state_fut => {},
        () = interface_control_fut => {},
    };
    if !interface_control_fut.is_terminated() {
        // Cancel interface control and drive it to completion, allowing it to terminate each
        // control handle.
        interface_control_stop_sender
            .send(fnet_interfaces_admin::InterfaceRemovedReason::PortClosed)
            .expect("failed to cancel interface control");
        interface_control_fut.await;
    }
    remove_interface(ctx, id).await;
}

/// Runs a worker to watch the given status_stream and update the interface state accordingly.
async fn run_link_state_watcher<
    S: futures::Stream<Item = netdevice_client::Result<netdevice_client::client::PortStatus>>,
>(
    ctx: NetstackContext,
    id: BindingId,
    status_stream: S,
) {
    let result = status_stream
        .try_for_each(|netdevice_client::client::PortStatus { flags, mtu: _ }| {
            let ctx = &ctx;
            async move {
                let online = flags.contains(fhardware_network::StatusFlags::ONLINE);
                log::debug!("observed interface {} online = {}", id, online);
                let mut ctx = ctx.lock().await;
                match ctx
                    .non_sync_ctx
                    .devices
                    .get_device_mut(id)
                    .expect("device not present")
                    .info_mut()
                {
                    devices::DeviceSpecificInfo::Netdevice(devices::NetdeviceInfo {
                        common_info: _,
                        handler: _,
                        mac: _,
                        phy_up,
                    }) => *phy_up = online,
                    i @ devices::DeviceSpecificInfo::Ethernet(_)
                    | i @ devices::DeviceSpecificInfo::Loopback(_) => {
                        unreachable!("unexpected device info {:?} for interface {}", i, id)
                    }
                };
                // Enable or disable interface with context depending on new online
                // status. The helper functions take care of checking if admin
                // enable is the expected value.
                if online {
                    ctx.enable_interface(id).expect("failed to enable interface");
                } else {
                    ctx.disable_interface(id).expect("failed to enable interface");
                }
                Ok(())
            }
        })
        .await;
    match result {
        Ok(()) => log::debug!("state stream closed for interface {}", id),
        Err(e) => {
            let level = match &e {
                netdevice_client::Error::Fidl(e) if e.is_closed() => log::Level::Debug,
                _ => log::Level::Error,
            };
            log::log!(level, "error operating port state stream {:?} for interface {}", e, id);
        }
    }
}

/// Runs a worker to serve incoming `fuchsia.net.interfaces.admin/Control` handles.
pub(crate) async fn run_interface_control(
    ctx: NetstackContext,
    id: BindingId,
    mut stop_receiver: futures::channel::oneshot::Receiver<
        fnet_interfaces_admin::InterfaceRemovedReason,
    >,
    control_receiver: futures::channel::mpsc::Receiver<OwnedControlHandle>,
) {
    // An event indicating that the individual control request streams should stop.
    let cancel_request_streams = async_utils::event::Event::new();
    // A struct to retain per-handle state of the individual request streams in `control_receiver`.
    struct ReqStreamState {
        owns_interface: bool,
        control_handle: fnet_interfaces_admin::ControlControlHandle,
        ctx: NetstackContext,
        id: BindingId,
    }
    // Convert `control_receiver` (a stream-of-streams) into a stream of futures, where each future
    // represents the termination of an inner `ControlRequestStream`.
    let stream_of_fut = control_receiver.map(
        |OwnedControlHandle { request_stream, control_handle, owns_interface }| {
            let initial_state =
                ReqStreamState { owns_interface, control_handle, ctx: ctx.clone(), id };
            // Attach `cancel_request_streams` as a short-circuit mechanism to stop handling new
            // `ControlRequest`.
            request_stream
                .take_until(cancel_request_streams.wait_or_dropped())
                // Convert the request stream into a future, dispatching each incoming
                // `ControlRequest` and retaining the `ReqStreamState` along the way.
                .fold(initial_state, |mut state, request| async move {
                    let ReqStreamState { ctx, id, owns_interface, control_handle: _ } = &mut state;
                    match request {
                        Err(e) => log::log!(
                            util::fidl_err_log_level(&e),
                            "error operating {} stream for interface {}: {:?}",
                            fnet_interfaces_admin::ControlMarker::DEBUG_NAME,
                            id,
                            e,
                        ),
                        Ok(req) => {
                            match dispatch_control_request(req, ctx, *id, owns_interface).await {
                                Err(e) => {
                                    log::log!(
                                        util::fidl_err_log_level(&e),
                                        "failed to handle request for interface {}: {:?}",
                                        id,
                                        e
                                    )
                                }
                                Ok(()) => {}
                            }
                        }
                    }
                    // Return `state` to be used when handling the next `ControlRequest`.
                    state
                })
        },
    );
    // Enable the stream of futures to be polled concurrently.
    let mut stream_of_fut = stream_of_fut.buffer_unordered(std::usize::MAX);

    let remove_reason = {
        // Drive the `ControlRequestStreams` to completion, short-circuiting if an owner terminates.
        let interface_control_fut = async {
            while let Some(ReqStreamState { owns_interface, control_handle: _, ctx: _, id: _ }) =
                stream_of_fut.next().await
            {
                if owns_interface {
                    return;
                }
            }
        }
        .fuse();

        futures::pin_mut!(interface_control_fut);
        futures::select! {
            // One of the interface's owning channels hung up; inform the other channels.
            () = interface_control_fut => fnet_interfaces_admin::InterfaceRemovedReason::User,
            // Cancelation event was received with a specified remove reason.
            reason = stop_receiver => reason.expect("failed to receive stop"),
        }
    };

    // Close the control_receiver, preventing new RequestStreams from attaching.
    stream_of_fut.get_mut().get_mut().close();
    // Cancel the active request streams, and drive the remaining `ControlRequestStreams` to
    // completion, allowing each handle to send termination events.
    assert!(cancel_request_streams.signal(), "expected the event to be unsignaled");
    if !stream_of_fut.is_terminated() {
        while let Some(ReqStreamState { owns_interface: _, control_handle, ctx: _, id: _ }) =
            stream_of_fut.next().await
        {
            control_handle.send_on_interface_removed(remove_reason).unwrap_or_else(|e| {
                log::log!(
                    util::fidl_err_log_level(&e),
                    "failed to send terminal event: {:?} for interface {}",
                    e,
                    id
                )
            });
        }
    }
    // Cancel the `AddressStateProvider` workers and drive them to completion.
    let address_state_providers = {
        let mut ctx = ctx.lock().await;
        let device_info =
            ctx.non_sync_ctx.devices.get_device_mut(id).expect("missing device info for interface");
        futures::stream::FuturesUnordered::from_iter(
            device_info.info_mut().common_info_mut().addresses.values_mut().map(
                |devices::AddressInfo {
                     address_state_provider: devices::FidlWorkerInfo { worker, cancelation_sender },
                     assignment_state_sender: _,
                 }| {
                    if let Some(cancelation_sender) = cancelation_sender.take() {
                        cancelation_sender
                            .send(fnet_interfaces_admin::AddressRemovalReason::InterfaceRemoved)
                            .expect("failed to stop AddressStateProvider");
                    }
                    worker.clone()
                },
            ),
        )
    };
    address_state_providers.collect::<()>().await;
}

/// Serves a `fuchsia.net.interfaces.admin/Control` Request.
async fn dispatch_control_request(
    req: fnet_interfaces_admin::ControlRequest,
    ctx: &NetstackContext,
    id: BindingId,
    owns_interface: &mut bool,
) -> Result<(), fidl::Error> {
    log::debug!("serving {:?}", req);
    match req {
        fnet_interfaces_admin::ControlRequest::AddAddress {
            address,
            parameters,
            address_state_provider,
            control_handle: _,
        } => Ok(add_address(ctx, id, address, parameters, address_state_provider).await),
        fnet_interfaces_admin::ControlRequest::RemoveAddress { address, responder } => {
            responder.send(&mut Ok(remove_address(ctx, id, address).await))
        }
        fnet_interfaces_admin::ControlRequest::GetId { responder } => responder.send(id),
        fnet_interfaces_admin::ControlRequest::SetConfiguration { config, responder } => {
            // Lie in the response if forwarding is disabled to allow testing
            // with netstack3.
            if config.ipv4.map_or(false, |c| c.forwarding == Some(false))
                && config.ipv6.map_or(false, |c| c.forwarding == Some(false))
            {
                log::error!("https://fxbug.dev/76987 support enable/disable forwarding");
                responder.send(&mut Ok(fnet_interfaces_admin::Configuration::EMPTY))
            } else {
                todo!("https://fxbug.dev/76987 support enable/disable forwarding")
            }
        }
        fnet_interfaces_admin::ControlRequest::GetConfiguration { responder: _ } => {
            todo!("https://fxbug.dev/76987 support enable/disable forwarding")
        }
        fnet_interfaces_admin::ControlRequest::Enable { responder } => {
            responder.send(&mut Ok(set_interface_enabled(ctx, true, id).await))
        }
        fnet_interfaces_admin::ControlRequest::Disable { responder } => {
            responder.send(&mut Ok(set_interface_enabled(ctx, false, id).await))
        }
        fnet_interfaces_admin::ControlRequest::Detach { control_handle: _ } => {
            *owns_interface = false;
            Ok(())
        }
    }
}

/// Cleans up and removes the specified NetDevice interface.
async fn remove_interface(ctx: NetstackContext, id: BindingId) {
    let device_info = {
        let mut ctx = ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
        let info = non_sync_ctx
            .devices
            .remove_device(id)
            .expect("device lifetime should be tied to channel lifetime");
        netstack3_core::device::remove_device(sync_ctx, non_sync_ctx, info.core_id().clone());
        info
    };
    let handler = match device_info.into_info() {
        devices::DeviceSpecificInfo::Netdevice(devices::NetdeviceInfo {
            handler,
            common_info: _,
            mac: _,
            phy_up: _,
        }) => handler,
        i @ devices::DeviceSpecificInfo::Ethernet(_)
        | i @ devices::DeviceSpecificInfo::Loopback(_) => {
            unreachable!("unexpected device info {:?} for interface {}", i, id)
        }
    };
    handler.uninstall().await.unwrap_or_else(|e| {
        log::warn!("error uninstalling netdevice handler for interface {}: {:?}", id, e)
    })
}

/// Sets interface with `id` to `admin_enabled = enabled`.
///
/// Returns `true` if the value of `admin_enabled` changed in response to
/// this call.
async fn set_interface_enabled(ctx: &NetstackContext, enabled: bool, id: BindingId) -> bool {
    let mut ctx = ctx.lock().await;
    let (common_info, port_handler) =
        match ctx.non_sync_ctx.devices.get_device_mut(id).expect("device not present").info_mut() {
            devices::DeviceSpecificInfo::Ethernet(devices::EthernetInfo {
                common_info,
                // NB: In theory we should also start and stop the ethernet
                // device when we enable and disable, we'll skip that because
                // it's work and Ethernet is going to be deleted soon.
                client: _,
                mac: _,
                features: _,
                phy_up: _,
                interface_control: _,
            })
            | devices::DeviceSpecificInfo::Loopback(devices::LoopbackInfo {
                common_info,
                rx_notifier: _,
            }) => (common_info, None),
            devices::DeviceSpecificInfo::Netdevice(devices::NetdeviceInfo {
                common_info,
                handler,
                mac: _,
                phy_up: _,
            }) => (common_info, Some(handler)),
        };
    // Already set to expected value.
    if common_info.admin_enabled == enabled {
        return false;
    }
    common_info.admin_enabled = enabled;
    if let Some(handler) = port_handler {
        let r = if enabled { handler.attach().await } else { handler.detach().await };
        match r {
            Ok(()) => (),
            Err(e) => {
                log::warn!("failed to set port {:?} to {}: {:?}", handler, enabled, e);
                // NB: There might be other errors here to consider in the
                // future, we start with a very strict set of known errors to
                // allow and panic on anything that is unexpected.
                match e {
                    // We can race with the port being removed or the device
                    // being destroyed.
                    netdevice_client::Error::Attach(_, zx::Status::NOT_FOUND)
                    | netdevice_client::Error::Detach(_, zx::Status::NOT_FOUND) => (),
                    netdevice_client::Error::Fidl(e) if e.is_closed() => (),
                    e => panic!(
                        "unexpected error setting enabled={} on port {:?}: {:?}",
                        enabled, handler, e
                    ),
                }
            }
        }
    }
    if enabled {
        ctx.enable_interface(id).expect("failed to enable interface");
    } else {
        ctx.disable_interface(id).expect("failed to disable interface");
    }
    true
}

/// Removes the given `address` from the interface with the given `id`.
///
/// Returns `true` if the address existed and was removed; otherwise `false`.
async fn remove_address(ctx: &NetstackContext, id: BindingId, address: fnet::Subnet) -> bool {
    let specified_addr = match address.addr.try_into_core() {
        Ok(addr) => addr,
        Err(e) => {
            log::warn!("not removing unspecified address {:?}: {:?}", address.addr, e);
            return false;
        }
    };
    let (worker, cancelation_sender) = {
        let mut ctx = ctx.lock().await;
        let device_info =
            ctx.non_sync_ctx.devices.get_device_mut(id).expect("missing device info for interface");
        match device_info.info_mut().common_info_mut().addresses.get_mut(&specified_addr) {
            None => return false,
            Some(devices::AddressInfo {
                address_state_provider: devices::FidlWorkerInfo { worker, cancelation_sender },
                assignment_state_sender: _,
            }) => (worker.clone(), cancelation_sender.take()),
        }
    };
    let did_cancel_worker = match cancelation_sender {
        Some(cancelation_sender) => {
            cancelation_sender
                .send(fnet_interfaces_admin::AddressRemovalReason::UserRemoved)
                .expect("failed to stop AddressStateProvider");
            true
        }
        // The worker was already canceled by some other task.
        None => false,
    };
    // Wait for the worker to finish regardless of if we were the task to cancel
    // it. Doing so prevents us from prematurely returning while the address is
    // pending cleanup.
    worker.await;
    // Because the worker removes the address on teardown, `did_cancel_worker`
    // is a suitable proxy for `did_remove_addr`.
    return did_cancel_worker;
}

/// Adds the given `address` to the interface with the given `id`.
///
/// If the address cannot be added, the appropriate removal reason will be sent
/// to the address_state_provider.
async fn add_address(
    ctx: &NetstackContext,
    id: BindingId,
    address: fnet::Subnet,
    params: fnet_interfaces_admin::AddressParameters,
    address_state_provider: ServerEnd<fnet_interfaces_admin::AddressStateProviderMarker>,
) {
    let (req_stream, control_handle) = address_state_provider
        .into_stream_and_control_handle()
        .expect("failed to decompose AddressStateProvider handle");
    let addr_subnet_either: AddrSubnetEither = match address.try_into_core() {
        Ok(addr) => addr,
        Err(e) => {
            log::warn!("not adding invalid address {:?} to interface {}: {:?}", address, id, e);
            close_address_state_provider(
                address.addr.into_core(),
                id,
                control_handle,
                fnet_interfaces_admin::AddressRemovalReason::Invalid,
            );
            return;
        }
    };
    let specified_addr = addr_subnet_either.addr();

    if params.temporary.unwrap_or(false) {
        todo!("https://fxbug.dev/105630: support adding temporary addresses");
    }
    const INFINITE_NANOS: i64 = zx::Time::INFINITE.into_nanos();
    let initial_properties =
        params.initial_properties.unwrap_or(fnet_interfaces_admin::AddressProperties::EMPTY);
    let valid_lifetime_end = initial_properties.valid_lifetime_end.unwrap_or(INFINITE_NANOS);
    if valid_lifetime_end != INFINITE_NANOS {
        log::warn!(
            "TODO(https://fxbug.dev/105630): ignoring valid_lifetime_end: {:?}",
            valid_lifetime_end
        );
    }
    match initial_properties.preferred_lifetime_info.unwrap_or(
        fnet_interfaces_admin::PreferredLifetimeInfo::PreferredLifetimeEnd(INFINITE_NANOS),
    ) {
        fnet_interfaces_admin::PreferredLifetimeInfo::Deprecated(_) => {
            todo!("https://fxbug.dev/105630: support adding deprecated addresses")
        }
        fnet_interfaces_admin::PreferredLifetimeInfo::PreferredLifetimeEnd(
            preferred_lifetime_end,
        ) => {
            if preferred_lifetime_end != INFINITE_NANOS {
                log::warn!(
                    "TODO(https://fxbug.dev/105630): ignoring preferred_lifetime_end: {:?}",
                    preferred_lifetime_end
                );
            }
        }
    }

    let mut locked_ctx = ctx.lock().await;
    let device_info = locked_ctx
        .non_sync_ctx
        .devices
        .get_device_mut(id)
        .expect("missing device info for interface");
    let vacant_address_entry =
        match device_info.info_mut().common_info_mut().addresses.entry(specified_addr) {
            hash_map::Entry::Occupied(_occupied) => {
                close_address_state_provider(
                    address.addr.into_core(),
                    id,
                    control_handle,
                    fnet_interfaces_admin::AddressRemovalReason::AlreadyAssigned,
                );
                return;
            }
            hash_map::Entry::Vacant(vacant) => vacant,
        };
    // Cancelation mechanism for the `AddressStateProvider` worker.
    let (cancelation_sender, cancelation_receiver) = futures::channel::oneshot::channel();
    // Sender/receiver for updates in `AddressAssignmentState`, as
    // published by Core.
    let (assignment_state_sender, assignment_state_receiver) = futures::channel::mpsc::unbounded();
    // Spawn the `AddressStateProvider` worker, which during
    // initialization, will add the address to Core.
    let worker = fasync::Task::spawn(run_address_state_provider(
        ctx.clone(),
        addr_subnet_either,
        id,
        control_handle,
        req_stream,
        assignment_state_receiver,
        cancelation_receiver,
    ))
    .shared();
    let _: &mut devices::AddressInfo = vacant_address_entry.insert(devices::AddressInfo {
        address_state_provider: devices::FidlWorkerInfo {
            worker,
            cancelation_sender: Some(cancelation_sender),
        },
        assignment_state_sender,
    });
}

/// A worker for `fuchsia.net.interfaces.admin/AddressStateProvider`.
async fn run_address_state_provider(
    ctx: NetstackContext,
    addr_subnet_either: AddrSubnetEither,
    id: BindingId,
    control_handle: fnet_interfaces_admin::AddressStateProviderControlHandle,
    req_stream: fnet_interfaces_admin::AddressStateProviderRequestStream,
    mut assignment_state_receiver: futures::channel::mpsc::UnboundedReceiver<
        fnet_interfaces_admin::AddressAssignmentState,
    >,
    mut stop_receiver: futures::channel::oneshot::Receiver<
        fnet_interfaces_admin::AddressRemovalReason,
    >,
) {
    let address = addr_subnet_either.addr();
    // Add the address to Core. Note that even though we verified the address
    // was absent from `ctx` before spawning this worker, it's still possible
    // for the address to exist in core (e.g. auto-configured addresses such as
    // loopback or SLAAC).
    let add_to_core_result = {
        let mut ctx = ctx.lock().await;
        let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
        let device_id = non_sync_ctx.devices.get_core_id(id).expect("interface not found");
        netstack3_core::add_ip_addr_subnet(sync_ctx, non_sync_ctx, &device_id, addr_subnet_either)
    };
    let should_remove_from_core = match add_to_core_result {
        Err(netstack3_core::error::ExistsError) => {
            close_address_state_provider(
                *address,
                id,
                control_handle,
                fnet_interfaces_admin::AddressRemovalReason::AlreadyAssigned,
            );
            // The address already existed, so don't attempt to remove it.
            // Otherwise we would accidentally remove an address we didn't add!
            false
        }
        Ok(()) => {
            // Receive the initial assignment state from Core. The message
            // must already be in the channel, so don't await.
            let initial_assignment_state = assignment_state_receiver
                .next()
                .now_or_never()
                .expect("receiver unexpectedly empty")
                .expect("sender unexpectedly closed");
            // Run the `AddressStateProvider` main loop.
            // Pass in the `assignment_state_receiver` and `stop_receiver` by
            // ref so that they don't get dropped after the main loop exits
            // (before the senders are removed from `ctx`).
            match address_state_provider_main_loop(
                address,
                id,
                control_handle,
                req_stream,
                &mut assignment_state_receiver,
                initial_assignment_state,
                &mut stop_receiver,
            )
            .await
            {
                AddressNeedsExplicitRemovalFromCore::Yes => true,
                AddressNeedsExplicitRemovalFromCore::No => false,
            }
        }
    };

    // Remove the address.
    let mut ctx = ctx.lock().await;
    let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
    let device_info =
        non_sync_ctx.devices.get_device_mut(id).expect("missing device info for interface");
    // Don't drop the worker yet; it's what's driving THIS function.
    let _worker: futures::future::Shared<fuchsia_async::Task<()>> =
        match device_info.info_mut().common_info_mut().addresses.remove(&address) {
            Some(devices::AddressInfo {
                address_state_provider: devices::FidlWorkerInfo { worker, cancelation_sender: _ },
                assignment_state_sender: _,
            }) => worker,
            None => {
                panic!("`AddressInfo` unexpectedly missing for {:?}", address)
            }
        };
    if should_remove_from_core {
        let device_id = non_sync_ctx.devices.get_core_id(id).expect("interface not found");
        assert_matches!(
            netstack3_core::del_ip_addr(sync_ctx, non_sync_ctx, &device_id, address),
            Ok(())
        );
    }
}

enum AddressNeedsExplicitRemovalFromCore {
    /// The caller is expected to delete the address from Core.
    Yes,
    /// The caller need not delete the address from Core (e.g. interface removal,
    /// which implicitly removes all addresses.)
    No,
}

async fn address_state_provider_main_loop(
    address: SpecifiedAddr<IpAddr>,
    id: BindingId,
    control_handle: fnet_interfaces_admin::AddressStateProviderControlHandle,
    req_stream: fnet_interfaces_admin::AddressStateProviderRequestStream,
    assignment_state_receiver: &mut futures::channel::mpsc::UnboundedReceiver<
        fnet_interfaces_admin::AddressAssignmentState,
    >,
    initial_assignment_state: fnet_interfaces_admin::AddressAssignmentState,
    stop_receiver: &mut futures::channel::oneshot::Receiver<
        fnet_interfaces_admin::AddressRemovalReason,
    >,
) -> AddressNeedsExplicitRemovalFromCore {
    // When detached, the lifetime of `req_stream` should not be tied to the
    // lifetime of `address`.
    let mut detached = false;
    let mut watch_state = AddressAssignmentWatcherState {
        fsm: AddressAssignmentWatcherStateMachine::UnreportedUpdate(initial_assignment_state),
        last_response: None,
    };
    enum AddressStateProviderEvent {
        Request(Result<Option<fnet_interfaces_admin::AddressStateProviderRequest>, fidl::Error>),
        AssignmentStateChange(fnet_interfaces_admin::AddressAssignmentState),
        Canceled(fnet_interfaces_admin::AddressRemovalReason),
    }
    futures::pin_mut!(req_stream);
    futures::pin_mut!(stop_receiver);
    futures::pin_mut!(assignment_state_receiver);
    let cancelation_reason = loop {
        let next_event = futures::select! {
            req = req_stream.try_next() => AddressStateProviderEvent::Request(req),
            state = assignment_state_receiver.next() => {
                    AddressStateProviderEvent::AssignmentStateChange(
                        // It's safe to `expect` here, because the
                        // AddressStateProvider worker is responsible for
                        // cleaning up the sender, and only does so after this
                        // function exits.
                        state.expect("sender unexpectedly closed"))
                },
            reason = stop_receiver => AddressStateProviderEvent::Canceled(reason.expect("failed to receive stop")),
        };
        match next_event {
            AddressStateProviderEvent::Request(req) => match req {
                // The client hung up, stop serving.
                Ok(None) => {
                    // If detached, wait to be canceled before exiting.
                    if detached {
                        // N.B. The `Canceled` arm of this match exits the loop,
                        // meaning we can't already be canceled here.
                        debug_assert!(!stop_receiver.is_terminated());
                        break Some(stop_receiver.await.expect("failed to receive stop"));
                    }
                    break None;
                }
                Ok(Some(request)) => {
                    let e = match dispatch_address_state_provider_request(
                        request,
                        &mut detached,
                        &mut watch_state,
                    )
                    .await
                    {
                        Ok(()) => continue,
                        Err(e) => e,
                    };
                    let (log_level, should_terminate) = match &e {
                        AddressStateProviderError::PreviousPendingWatchRequest => {
                            (log::Level::Warn, true)
                        }
                        AddressStateProviderError::Fidl(e) => (util::fidl_err_log_level(e), false),
                    };
                    log::log!(
                        log_level,
                        "failed to handle request for address {:?} on interface {}: {}",
                        address,
                        id,
                        e
                    );
                    if should_terminate {
                        break None;
                    }
                }
                Err(e) => {
                    log::log!(
                        util::fidl_err_log_level(&e),
                        "error operating {} stream for address {:?} on interface {}: {:?}",
                        fnet_interfaces_admin::AddressStateProviderMarker::DEBUG_NAME,
                        address,
                        id,
                        e
                    );
                    break None;
                }
            },
            AddressStateProviderEvent::AssignmentStateChange(state) => {
                watch_state.on_new_assignment_state(state).unwrap_or_else(|e|{
                        log::log!(
                            util::fidl_err_log_level(&e),
                            "failed to respond to pending watch request for address {:?} on interface {}: {:?}",
                            address,
                            id,
                            e
                        )
                    });
            }
            AddressStateProviderEvent::Canceled(reason) => {
                close_address_state_provider(*address, id, control_handle, reason);
                break Some(reason);
            }
        }
    };

    match cancelation_reason {
        Some(fnet_interfaces_admin::AddressRemovalReason::InterfaceRemoved) => {
            return AddressNeedsExplicitRemovalFromCore::No
        }
        Some(fnet_interfaces_admin::AddressRemovalReason::Invalid)
        | Some(fnet_interfaces_admin::AddressRemovalReason::AlreadyAssigned)
        | Some(fnet_interfaces_admin::AddressRemovalReason::DadFailed)
        | Some(fnet_interfaces_admin::AddressRemovalReason::UserRemoved)
        | None => AddressNeedsExplicitRemovalFromCore::Yes,
    }
}

#[derive(thiserror::Error, Debug)]
pub(crate) enum AddressStateProviderError {
    #[error(
        "received a `WatchAddressAssignmentState` request while a previous request is pending"
    )]
    PreviousPendingWatchRequest,
    #[error("FIDL error: {0}")]
    Fidl(fidl::Error),
}

// State Machine for the `WatchAddressAssignmentState` "Hanging-Get" FIDL API.
#[derive(Debug)]
enum AddressAssignmentWatcherStateMachine {
    // Holds the new assignment state waiting to be sent.
    UnreportedUpdate(fnet_interfaces_admin::AddressAssignmentState),
    // Holds the hanging responder waiting for a new assignment state to send.
    HangingRequest(fnet_interfaces_admin::AddressStateProviderWatchAddressAssignmentStateResponder),
    Idle,
}
struct AddressAssignmentWatcherState {
    // The state of the `WatchAddressAssignmentState` "Hanging-Get" FIDL API.
    fsm: AddressAssignmentWatcherStateMachine,
    // The last response to a `WatchAddressAssignmentState` FIDL request.
    // `None` until the first request, after which it will always be `Some`.
    last_response: Option<fnet_interfaces_admin::AddressAssignmentState>,
}

impl AddressAssignmentWatcherState {
    // Handle a change in `AddressAssignmentState` as published by Core.
    fn on_new_assignment_state(
        &mut self,
        new_state: fnet_interfaces_admin::AddressAssignmentState,
    ) -> Result<(), fidl::Error> {
        use AddressAssignmentWatcherStateMachine::*;
        let Self { fsm, last_response } = self;
        // Use `Idle` as a placeholder value to take ownership of `fsm`.
        let old_fsm = std::mem::replace(fsm, Idle);
        let (new_fsm, result) = match old_fsm {
            UnreportedUpdate(old_state) => {
                if old_state == new_state {
                    log::warn!("received duplicate AddressAssignmentState event from Core.");
                }
                if self.last_response == Some(new_state) {
                    // Return to `Idle` because we've coalesced
                    // multiple updates and no-longer have new state to send.
                    (Idle, Ok(()))
                } else {
                    (UnreportedUpdate(new_state), Ok(()))
                }
            }
            HangingRequest(responder) => {
                *last_response = Some(new_state);
                (Idle, responder.send(new_state))
            }
            Idle => (UnreportedUpdate(new_state), Ok(())),
        };
        assert_matches!(std::mem::replace(fsm, new_fsm), Idle);
        result
    }

    // Handle a new `WatchAddressAssignmentState` FIDL request.
    fn on_new_watch_req(
        &mut self,
        responder: fnet_interfaces_admin::AddressStateProviderWatchAddressAssignmentStateResponder,
    ) -> Result<(), AddressStateProviderError> {
        use AddressAssignmentWatcherStateMachine::*;
        let Self { fsm, last_response } = self;
        // Use `Idle` as a placeholder value to take ownership of `fsm`.
        let old_fsm = std::mem::replace(fsm, Idle);
        let (new_fsm, result) = match old_fsm {
            UnreportedUpdate(state) => {
                *last_response = Some(state);
                (Idle, responder.send(state).map_err(AddressStateProviderError::Fidl))
            }
            HangingRequest(_existing_responder) => (
                HangingRequest(responder),
                Err(AddressStateProviderError::PreviousPendingWatchRequest),
            ),
            Idle => (HangingRequest(responder), Ok(())),
        };
        assert_matches!(std::mem::replace(fsm, new_fsm), Idle);
        result
    }
}

/// Serves a `fuchsia.net.interfaces.admin/AddressStateProvider` request.
async fn dispatch_address_state_provider_request(
    req: fnet_interfaces_admin::AddressStateProviderRequest,
    detached: &mut bool,
    watch_state: &mut AddressAssignmentWatcherState,
) -> Result<(), AddressStateProviderError> {
    log::debug!("serving {:?}", req);
    match req {
        fnet_interfaces_admin::AddressStateProviderRequest::UpdateAddressProperties {
            address_properties: _,
            responder: _,
        } => todo!("https://fxbug.dev/105011 Support updating address properties"),
        fnet_interfaces_admin::AddressStateProviderRequest::WatchAddressAssignmentState {
            responder,
        } => watch_state.on_new_watch_req(responder),
        fnet_interfaces_admin::AddressStateProviderRequest::Detach { control_handle: _ } => {
            *detached = true;
            Ok(())
        }
    }
}

fn close_address_state_provider(
    addr: IpAddr,
    id: BindingId,
    control_handle: fnet_interfaces_admin::AddressStateProviderControlHandle,
    reason: fnet_interfaces_admin::AddressRemovalReason,
) {
    control_handle.send_on_address_removed(reason).unwrap_or_else(|e| {
        let log_level = if e.is_closed() { log::Level::Debug } else { log::Level::Error };
        log::log!(
            log_level,
            "failed to send address removal reason for addr {:?} on interface {}: {:?}",
            addr,
            id,
            e,
        );
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
    use net_declare::fidl_subnet;
    use std::collections::HashMap;

    use crate::bindings::{
        interfaces_watcher::InterfaceEvent, interfaces_watcher::InterfaceUpdate, util::IntoFidl,
        InterfaceEventProducer, NetstackContext,
    };

    // Verifies that when an an interface is removed, its addresses are
    // implicitly removed, rather then explicitly removed one-by-one. Explicit
    // removal would be redundant and is unnecessary.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn implicit_address_removal_on_interface_removal() {
        let ctx: NetstackContext = Default::default();
        let (control_client_end, control_server_end) =
            fnet_interfaces_ext::admin::Control::create_endpoints().expect("create control proxy");
        let (control_sender, control_receiver) =
            OwnedControlHandle::new_channel_with_owned_handle(control_server_end).await;
        let (event_sender, event_receiver) = futures::channel::mpsc::unbounded();

        // Add the interface.
        const MTU: u32 = 65536;
        let build_fake_dev_info = |id| {
            const LOOPBACK_NAME: &'static str = "lo";
            devices::DeviceSpecificInfo::Loopback(devices::LoopbackInfo {
                common_info: devices::CommonInfo {
                    mtu: MTU,
                    admin_enabled: true,
                    events: InterfaceEventProducer::new(id, event_sender),
                    name: LOOPBACK_NAME.to_string(),
                    control_hook: control_sender,
                    addresses: HashMap::new(),
                },
                rx_notifier: Default::default(),
            })
        };
        let binding_id = {
            let mut ctx = ctx.lock().await;
            let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();
            let core_id = netstack3_core::device::add_loopback_device(sync_ctx, non_sync_ctx, MTU)
                .expect("failed to add loopback to core");
            non_sync_ctx
                .devices
                .add_device(core_id, build_fake_dev_info)
                .expect("failed to add loopback to bindings")
        };

        // Start the interface control worker.
        let (_stop_sender, stop_receiver) = futures::channel::oneshot::channel();
        let interface_control_fut =
            run_interface_control(ctx, binding_id, stop_receiver, control_receiver);

        // Add an address.
        let (asp_client_end, asp_server_end) =
            fidl::endpoints::create_proxy::<fnet_interfaces_admin::AddressStateProviderMarker>()
                .expect("create ASP proxy");
        let mut addr = fidl_subnet!("1.1.1.1/32");
        control_client_end
            .add_address(&mut addr, fnet_interfaces_admin::AddressParameters::EMPTY, asp_server_end)
            .expect("failed to add address");

        // Observe the `AddressAdded` event.
        let event_receiver = event_receiver.fuse();
        let interface_control_fut = interface_control_fut.fuse();
        futures::pin_mut!(event_receiver);
        futures::pin_mut!(interface_control_fut);
        let event = futures::select!(
            () = interface_control_fut => panic!("interface control unexpectedly ended"),
            event = event_receiver.next() => event
        );
        assert_matches!(event, Some(InterfaceEvent::Changed {
            id,
            event: InterfaceUpdate::AddressAdded {
                addr: address,
                assignment_state: _,
                valid_until: _,
            }
        }) if (id == binding_id && address.into_fidl() == addr ));

        // Drop the control handle and expect interface control to exit
        drop(control_client_end);
        interface_control_fut.await;

        // Expect that the event receiver has closed, and that it does not
        // contain, an `AddressRemoved` event, which would indicate the address
        // was explicitly removed.
        assert_matches!(event_receiver.next().await,
            Some(InterfaceEvent::Removed( id)) if id == binding_id);
        assert_matches!(event_receiver.next().await, None);

        // Verify the ASP closed for the correct reason.
        let fnet_interfaces_admin::AddressStateProviderEvent::OnAddressRemoved { error: reason } =
            asp_client_end
                .take_event_stream()
                .try_next()
                .await
                .expect("read AddressStateProvider event")
                .expect("AddressStateProvider event stream unexpectedly empty");
        assert_eq!(reason, fnet_interfaces_admin::AddressRemovalReason::InterfaceRemoved)
    }
}
