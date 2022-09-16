// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A Netstack3 worker to serve fuchsia.net.debug.Interfaces API requests.

use async_utils::channel::TrySend as _;
use fidl::endpoints::{ControlHandle as _, ProtocolMarker as _, ServerEnd};
use fidl_fuchsia_hardware_network as fhardware_network;
use fidl_fuchsia_net_debug as fnet_debug;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fuchsia_zircon as zx;
use futures::{SinkExt as _, StreamExt as _, TryStreamExt as _};
use tracing::{debug, error, warn};

use crate::bindings::{
    devices::LOOPBACK_MAC, interfaces_admin, util::IntoFidl, DeviceSpecificInfo, Netstack,
};

// Serve a stream of fuchsia.net.debug.Interfaces API requests for a single
// channel (e.g. a single client connection).
pub(crate) async fn serve_interfaces(
    ns: Netstack,
    rs: fnet_debug::InterfacesRequestStream,
) -> Result<(), fidl::Error> {
    debug!(protocol = fnet_debug::InterfacesMarker::DEBUG_NAME, "serving");
    rs.try_for_each(|req| async {
        match req {
            fnet_debug::InterfacesRequest::GetAdmin { id, control, control_handle: _ } => {
                handle_get_admin(&ns, id, control).await;
            }
            fnet_debug::InterfacesRequest::GetMac { id, responder } => {
                responder_send!(responder, &mut handle_get_mac(&ns, id).await);
            }
            fnet_debug::InterfacesRequest::GetPort { id, port, control_handle: _ } => {
                handle_get_port(&ns, id, port).await;
            }
        }
        Ok(())
    })
    .await
}

async fn handle_get_admin(
    ns: &Netstack,
    interface_id: u64,
    control: ServerEnd<fnet_interfaces_admin::ControlMarker>,
) {
    debug!(interface_id, "handling fuchsia.net.debug.Interfaces::GetAdmin");
    let mut ctx = ns.ctx.lock().await;
    let device_info = match ctx.non_sync_ctx.devices.get_device_mut(interface_id) {
        Some(device_info) => device_info,
        None => {
            control.close_with_epitaph(zx::Status::NOT_FOUND).unwrap_or_else(|e| {
                if e.is_closed() {
                    debug!(err = ?e, "control handle closed before sending epitaph")
                } else {
                    error!(err = ?e, "failed to send epitaph")
                }
            });
            return;
        }
    };

    match device_info
        .info_mut()
        .common_info_mut()
        .control_hook
        .try_send_fut(interfaces_admin::OwnedControlHandle::new_unowned(control))
        .await
    {
        Ok(()) => {}
        Err(owned_control_handle) => {
            owned_control_handle.into_control_handle().shutdown_with_epitaph(zx::Status::NOT_FOUND)
        }
    }
}

async fn handle_get_mac(ns: &Netstack, interface_id: u64) -> fnet_debug::InterfacesGetMacResult {
    debug!(interface_id, "handling fuchsia.net.debug.Interfaces::GetMac");
    let ctx = ns.ctx.lock().await;
    ctx.non_sync_ctx
        .devices
        .get_device(interface_id)
        .ok_or(fnet_debug::InterfacesGetMacError::NotFound)
        .map(|device_info| {
            let mac = match device_info.info() {
                DeviceSpecificInfo::Loopback(_) => LOOPBACK_MAC,
                DeviceSpecificInfo::Ethernet(info) => info.mac.into(),
                DeviceSpecificInfo::Netdevice(info) => info.mac.into(),
            };
            Some(Box::new(mac.into_fidl()))
        })
}

async fn handle_get_port(
    ns: &Netstack,
    interface_id: u64,
    port: ServerEnd<fhardware_network::PortMarker>,
) {
    let ctx = ns.ctx.lock().await;
    let port_handler =
        ctx.non_sync_ctx.devices.get_device(interface_id).ok_or(zx::Status::NOT_FOUND).and_then(
            |device_info| match device_info.info() {
                DeviceSpecificInfo::Loopback(_) | DeviceSpecificInfo::Ethernet(_) => {
                    Err(zx::Status::NOT_SUPPORTED)
                }
                DeviceSpecificInfo::Netdevice(info) => Ok(&info.handler),
            },
        );
    match port_handler {
        Ok(port_handler) => port_handler.connect_port(port).unwrap_or_else(
            |e: netdevice_client::Error| warn!(err = ?e, "failed to connect to port"),
        ),
        Err(epitaph) => {
            port.close_with_epitaph(epitaph)
                .unwrap_or_else(|e| warn!(err = ?e, "failed to send epitaph"));
        }
    }
}

struct DiagnosticsInner {
    thread: std::thread::JoinHandle<()>,
    sender: futures::channel::mpsc::Sender<ServerEnd<fnet_debug::DiagnosticsMarker>>,
}

impl DiagnosticsInner {
    fn new() -> Self {
        let (sender, mut receiver) =
            futures::channel::mpsc::channel::<ServerEnd<fnet_debug::DiagnosticsMarker>>(0);
        let thread = std::thread::spawn(move || {
            let mut executor =
                fuchsia_async::LocalExecutor::new().expect("failed to create diagnostics executor");
            let fut = async move {
                let mut futures = futures::stream::FuturesUnordered::new();
                loop {
                    let result = futures::select! {
                        s = receiver.next() => s,
                        n = futures.next() => {
                            // We don't care if FuturesUnordered
                            // finished, we might push more data into it
                            // later.
                            n.unwrap_or(());
                            continue
                        },
                    };
                    match result {
                        Some(rs) => {
                            futures.push(DiagnosticsHandler::serve_request_stream(
                                rs.into_stream().expect("failed to create request stream"),
                            ));
                        }
                        None => {
                            // When the receiver ends we want to stop
                            // serving all streams.
                            break;
                        }
                    }
                }
            };
            executor.run_singlethreaded(fut)
        });
        Self { sender, thread }
    }
}

/// Offers a server implementation of `fuchsia.net.debug/Diagnostics` that
/// serves all requests in a dedicated thread, so diagnostics can be provided
/// even if the main stack's executor is blocked.
#[derive(Default)]
pub(crate) struct DiagnosticsHandler {
    inner: once_cell::sync::OnceCell<DiagnosticsInner>,
}

impl Drop for DiagnosticsHandler {
    fn drop(&mut self) {
        let Self { inner } = self;
        if let Some(DiagnosticsInner { thread, sender }) = inner.take() {
            // Drop the sender so the receiver side on the worker thread will
            // terminate.
            std::mem::drop(sender);
            thread.join().expect("failed to join diagnostics thread");
        }
    }
}

impl DiagnosticsHandler {
    pub(crate) async fn serve_diagnostics(
        &self,
        server_end: ServerEnd<fnet_debug::DiagnosticsMarker>,
    ) {
        let Self { inner } = self;
        let mut sender = {
            let DiagnosticsInner { sender, thread: _ } = inner.get_or_init(DiagnosticsInner::new);
            // Clone the sender, we don't have mutable access to it behind the
            // cell.
            sender.clone()
        };

        sender.send(server_end).await.expect("sender was orphaned unexpectedly");
    }

    async fn serve_request_stream(rs: fnet_debug::DiagnosticsRequestStream) {
        rs.try_for_each(|req| {
            futures::future::ready(match req {
                fnet_debug::DiagnosticsRequest::LogDebugInfoToSyslog { responder } => {
                    warn!(
                        "Requesting stack trace to logs as requested by {}, this is not a crash.",
                        fnet_debug::DiagnosticsMarker::DEBUG_NAME
                    );
                    backtrace_request::backtrace_request();
                    responder.send()
                }
            })
        })
        .await
        .unwrap_or_else(|e: fidl::Error| {
            if e.is_closed() {
                warn!(err = ?e, "error operating diagnostics stream");
            } else {
                error!(err = ?e, "error operating diagnostics stream");
            }
        });
    }
}

#[cfg(test)]
mod tests {
    use super::DiagnosticsHandler;
    use fuchsia_zircon as zx;
    use futures::StreamExt as _;
    use test_case::test_case;

    // DiagnosticsHandler has a nontrivial Drop path that isn't really exercised
    // in integration tests. This gets us some coverage.
    #[test_case(0; "empty")]
    #[test_case(3; "multiple")]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn drop_diagnostics_handler(streams: usize) {
        let handler = DiagnosticsHandler::default();

        // Attach channels.
        let channels = {
            let handler = &handler;
            futures::stream::repeat_with(|| {
                fidl::endpoints::create_endpoints().expect("create_endpoints")
            })
            .then(|(client, server)| async move {
                handler.serve_diagnostics(server).await;
                client.into_channel()
            })
            .take(streams)
            .collect::<Vec<_>>()
            .await
        };

        // Dropping the handler should stop the alternative executor and join
        // the thread.
        std::mem::drop(handler);

        for channel in channels {
            let signals =
                fuchsia_async::OnSignals::new(&channel, zx::Signals::CHANNEL_PEER_CLOSED).await;
            assert_eq!(signals, Ok(zx::Signals::CHANNEL_PEER_CLOSED));
        }
    }
}
