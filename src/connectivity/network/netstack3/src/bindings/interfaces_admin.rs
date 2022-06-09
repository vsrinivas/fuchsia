// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::DerefMut as _;

use fidl::endpoints::ProtocolMarker as _;
use fidl_fuchsia_hardware_network as fhardware_network;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;

use futures::{FutureExt as _, StreamExt as _, TryFutureExt as _, TryStreamExt as _};
use netstack3_core::Ctx;

use crate::bindings::{
    devices, netdevice_worker, BindingId, InterfaceControl as _, Netstack, NetstackContext,
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
    device_control: fnet_interfaces_admin::DeviceControlRequestStream,
) -> Result<(), DeviceControlError> {
    let worker = netdevice_worker::NetdeviceWorker::new(ns.ctx.clone(), device).await?;
    let handler = worker.new_handler();
    let worker_fut = worker.run().map_err(DeviceControlError::Worker);
    let (stop_trigger, stop_fut) = futures::channel::oneshot::channel::<()>();
    let stop_fut = stop_fut.map(|r| r.expect("closed all cancellation senders")).shared();
    let control_stream = device_control
        .take_until(stop_fut.clone())
        .map_err(DeviceControlError::Fidl)
        .try_filter_map(|req| match req {
            fnet_interfaces_admin::DeviceControlRequest::CreateInterface {
                port,
                control,
                options,
                control_handle: _,
            } => create_interface(port, control, options, &ns, &handler, &stop_fut),
            fnet_interfaces_admin::DeviceControlRequest::Detach { control_handle: _ } => {
                todo!("https://fxbug.dev/100867 support detach");
            }
        });
    futures::pin_mut!(worker_fut);
    futures::pin_mut!(control_stream);
    let mut tasks = futures::stream::FuturesUnordered::new();
    let res = loop {
        let mut tasks_fut = if tasks.is_empty() {
            futures::future::pending().left_future()
        } else {
            tasks.by_ref().next().right_future()
        };
        let result = futures::select! {
            r = control_stream.try_next() => r,
            r = worker_fut => match r {
                Ok(never) => match never {},
                Err(e) => Err(e)
            },
            ready_task = tasks_fut => {
                let () = ready_task.unwrap_or_else(|| ());
                continue;
            }
        };
        match result {
            Ok(Some(task)) => tasks.push(task),
            Ok(None) => break Ok(()),
            Err(e) => break Err(e),
        }
    };

    // Send a stop signal to all tasks.
    stop_trigger.send(()).expect("receiver should not be gone");
    match &res {
        // Control stream has finished, don't need to drain it.
        Ok(()) | Err(DeviceControlError::Fidl(_)) => (),
        Err(DeviceControlError::Worker(_)) => {
            // Drain control stream to make sure we have all the tasks. The stop
            // trigger will make it stop operating on new requests.
            control_stream
                .try_for_each(|t| futures::future::ok(tasks.push(t)))
                .await
                .unwrap_or_else(|e| log::warn!("failed to accumulate remaining tasks: {:?}", e));
        }
    }
    // Run all the tasks to completion. We sent the stop signal, they should all
    // complete and perform interface cleanup.
    tasks.collect::<()>().await;

    res
}

/// Operates a fuchsia.net.interfaces.admin/DeviceControl.CreateInterface
/// request.
///
/// Returns `Ok(Some(fuchsia_async::Task))` if an interface was created
/// successfully. The returned `Task` must be polled to completion and is tied
/// to the created interface's lifetime.
async fn create_interface(
    port: fhardware_network::PortId,
    control: fidl::endpoints::ServerEnd<fnet_interfaces_admin::ControlMarker>,
    options: fnet_interfaces_admin::Options,
    ns: &Netstack,
    handler: &netdevice_worker::DeviceHandler,
    stop_fut: &(impl futures::Future<Output = ()>
          + futures::future::FusedFuture
          + Clone
          + Send
          + 'static),
) -> Result<Option<fuchsia_async::Task<()>>, DeviceControlError> {
    log::debug!("creating interface from {:?} with {:?}", port, options);
    let fnet_interfaces_admin::Options { name, metric: _, .. } = options;
    match handler.add_port(&ns, netdevice_worker::InterfaceOptions { name }, port).await {
        Ok((binding_id, status_stream)) => Ok(Some(fasync::Task::spawn(run_interface_control(
            ns.ctx.clone(),
            binding_id,
            stop_fut.clone(),
            status_stream,
            control,
        )))),
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
                    | netdevice_client::Error::PeerClosed(_)
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
                netdevice_worker::Error::SystemResource(_)
                | netdevice_worker::Error::InvalidPortInfo(_) => None,
            };
            if let Some(removed_reason) = removed_reason {
                let (_stream, control) =
                    control.into_stream_and_control_handle().expect("failed to acquire stream");
                control.send_on_interface_removed(removed_reason).unwrap_or_else(|e| {
                    log::warn!("failed to send removed reason: {:?}", e);
                });
            }
            Ok(None)
        }
    }
}

async fn run_interface_control<
    F: Send + 'static + futures::Future<Output = ()> + futures::future::FusedFuture,
    S: futures::Stream<Item = netdevice_client::Result<netdevice_client::client::PortStatus>>,
>(
    ctx: NetstackContext,
    id: BindingId,
    cancel: F,
    status_stream: S,
    server_end: fidl::endpoints::ServerEnd<fnet_interfaces_admin::ControlMarker>,
) {
    let (mut stream, control_handle) =
        server_end.into_stream_and_control_handle().expect("failed to create stream");
    let stream_fut = async {
        while let Some(req) = stream.try_next().await? {
            log::debug!("serving {:?}", req);
            let () = match req {
                fnet_interfaces_admin::ControlRequest::AddAddress {
                    address: _,
                    parameters: _,
                    address_state_provider: _,
                    control_handle: _,
                } => todo!("https://fxbug.dev/100870 support add address"),
                fnet_interfaces_admin::ControlRequest::RemoveAddress {
                    address: _,
                    responder: _,
                } => {
                    todo!("https://fxbug.dev/100870 support remove address")
                }
                fnet_interfaces_admin::ControlRequest::GetId { responder } => responder.send(id),
                fnet_interfaces_admin::ControlRequest::SetConfiguration {
                    config: _,
                    responder: _,
                } => {
                    todo!("https://fxbug.dev/76987 support enable/disable forwarding")
                }
                fnet_interfaces_admin::ControlRequest::GetConfiguration { responder: _ } => {
                    todo!("https://fxbug.dev/76987 support enable/disable forwarding")
                }
                fnet_interfaces_admin::ControlRequest::Enable { responder } => {
                    responder.send(&mut Ok(set_interface_enabled(&ctx, true, id).await))
                }
                fnet_interfaces_admin::ControlRequest::Disable { responder } => {
                    responder.send(&mut Ok(set_interface_enabled(&ctx, false, id).await))
                }
                fnet_interfaces_admin::ControlRequest::Detach { control_handle: _ } => {
                    todo!("https://fxbug.dev/100867 support detach");
                }
            }?;
        }
        Result::<_, fidl::Error>::Ok(())
    }
    .fuse();

    let link_state_fut = status_stream
        .try_for_each(|netdevice_client::client::PortStatus { flags, mtu: _ }| {
            let ctx = &ctx;
            async move {
                let online = flags.contains(fhardware_network::StatusFlags::ONLINE);
                log::debug!("observed interface {} online = {}", id, online);
                let mut ctx = ctx.lock().await;
                match ctx
                    .sync_ctx
                    .dispatcher
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
        .fuse();

    enum Outcome {
        Cancelled,
        StreamEnded(Result<(), fidl::Error>),
        StateStreamEnded(Result<(), netdevice_client::Error>),
    }
    futures::pin_mut!(stream_fut);
    futures::pin_mut!(cancel);
    futures::pin_mut!(link_state_fut);
    let outcome = futures::select! {
        o = stream_fut => Outcome::StreamEnded(o),
        () = cancel => Outcome::Cancelled,
        o = link_state_fut => Outcome::StateStreamEnded(o),
    };
    let remove_reason = match outcome {
        Outcome::Cancelled => {
            // Device has been removed from under us, inform the user that's the
            // case.
            Some(fnet_interfaces_admin::InterfaceRemovedReason::PortClosed)
        }
        Outcome::StreamEnded(Err(e)) => {
            log::error!(
                "error operating {} stream: {:?} for interface {}",
                fnet_interfaces_admin::ControlMarker::DEBUG_NAME,
                e,
                id
            );
            None
        }
        Outcome::StateStreamEnded(r) => {
            match r {
                Ok(()) => log::debug!("state stream closed for interface {}", id),
                Err(e) => {
                    let level = match &e {
                        netdevice_client::Error::Fidl(e) if e.is_closed() => log::Level::Debug,
                        _ => log::Level::Error,
                    };
                    log::log!(
                        level,
                        "error operating port state stream {:?} for interface {}",
                        e,
                        id
                    );
                }
            }
            Some(fnet_interfaces_admin::InterfaceRemovedReason::PortClosed)
        }
        Outcome::StreamEnded(Ok(())) => None,
    };

    if let Some(remove_reason) = remove_reason {
        control_handle.send_on_interface_removed(remove_reason).unwrap_or_else(|e| {
            if !e.is_closed() {
                log::error!("failed to send terminal event: {:?} for interface {}", e, id)
            }
        });
    }

    // Cleanup and remove the interface.

    // TODO(https://fxbug.dev/88797): We're not supposed to cleanup if this is a
    // debug channel.
    // TODO(https://fxbug.dev/100867): We're not supposed to cleanup if we're
    // detached.

    let device_info = {
        let mut ctx = ctx.lock().await;
        let Ctx { sync_ctx } = ctx.deref_mut();
        let info = sync_ctx
            .dispatcher
            .devices
            .remove_device(id)
            .expect("device lifetime should be tied to channel lifetime");
        netstack3_core::remove_device(sync_ctx, info.core_id());
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
    let (common_info, port_handler) = match ctx
        .sync_ctx
        .dispatcher
        .devices
        .get_device_mut(id)
        .expect("device not present")
        .info_mut()
    {
        devices::DeviceSpecificInfo::Ethernet(devices::EthernetInfo {
            common_info,
            // NB: In theory we should also start and stop the ethernet
            // device when we enable and disable, we'll skip that because
            // it's work and Ethernet is going to be deleted soon.
            client: _,
            mac: _,
            features: _,
            phy_up: _,
        })
        | devices::DeviceSpecificInfo::Loopback(devices::LoopbackInfo { common_info }) => {
            (common_info, None)
        }
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
