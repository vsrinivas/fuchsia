// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use fidl::encoding2::OutOfLine;
use fidl::endpoints2::RequestStream;
use fidl_fuchsia_bluetooth;
use fidl_fuchsia_bluetooth_control::{ControlRequest, ControlRequestStream};
use futures::future::ok as fok;
use futures::future::Either::{Left, Right};
use futures::prelude::*;
use futures::{Future, FutureExt, Never};
use host_dispatcher::*;
use parking_lot::RwLock;
use std::sync::Arc;

struct ControlServiceState {
    host: Arc<RwLock<HostDispatcher>>,
    discovery_token: Option<Arc<DiscoveryRequestToken>>,
    discoverable_token: Option<Arc<DiscoverableRequestToken>>,
}

/// Build the ControlImpl to interact with fidl messages
/// State is stored in the HostDispatcher object
pub fn make_control_service(
    hd: Arc<RwLock<HostDispatcher>>, chan: async::Channel,
) -> impl Future<Item = (), Error = Never> {
    let state = Arc::new(RwLock::new(ControlServiceState {
        host: hd,
        discovery_token: None,
        discoverable_token: None,
    }));

    let mystate = state.clone();
    let wstate = mystate.write();
    let mut hd = wstate.host.write();

    let stream = ControlRequestStream::from_channel(chan);
    hd.event_listeners.push(stream.control_handle());
    many_futures!(Output, [A, B, C, D, E, F, G, H, I, J, K,]);

    stream
        .for_each(move |evt| match evt {
            ControlRequest::Connect {
                device_id: _,
                responder,
            } => {
                let _ = responder.send(&mut bt_fidl_status!(NotSupported));
                Output::C(fok(()))
            }
            ControlRequest::SetDiscoverable {
                discoverable,
                responder,
            } => {
                let fut = if discoverable {
                    let stateref = state.clone();
                    Left(
                        HostDispatcher::set_discoverable(state.read().host.clone()).and_then(
                            move |(mut resp, token)| {
                                stateref.write().discoverable_token = token;
                                responder.send(&mut resp).into_future()
                            },
                        ),
                    )
                } else {
                    state.write().discoverable_token = None;
                    Right(responder.send(&mut bt_fidl_status!()).into_future())
                };
                Output::D(fut)
            }
            ControlRequest::SetIoCapabilities { .. } => Output::E(fok(())),
            ControlRequest::IsBluetoothAvailable { responder } => {
                let rstate = state.read();
                let mut hd = rstate.host.write();
                let is_available = hd.get_active_adapter_info().is_some();
                let _ = responder.send(is_available);
                Output::F(fok(()))
            }
            ControlRequest::SetPairingDelegate {
                delegate,
                responder: _,
            } => {
                let mut wstate = state.write();
                if let Some(delegate) = delegate {
                    if let Ok(proxy) = delegate.into_proxy() {
                        wstate.host.write().pairing_delegate = Some(proxy);
                    }
                } else {
                    wstate.host.write().pairing_delegate = None;
                }
                Output::G(fok(()))
            }
            ControlRequest::GetAdapters { responder } => {
                let wstate = state.write();
                let mut hd = wstate.host.clone();
                let fut = HostDispatcher::get_adapters(&mut hd)
                    .and_then(move |mut resp| responder.send(Some(&mut resp.iter_mut())));
                Output::H(fut)
            }
            ControlRequest::SetActiveAdapter {
                identifier,
                responder,
            } => {
                let wstate = state.write();
                let mut success = wstate.host.write().set_active_adapter(identifier.clone());
                let _ = responder.send(&mut success);
                Output::I(fok(()))
            }
            ControlRequest::GetActiveAdapterInfo { responder } => {
                let wstate = state.write();
                let mut hd = wstate.host.write();
                let mut adap = hd.get_active_adapter_info();

                let _ = responder.send(adap.as_mut().map(OutOfLine));
                Output::J(fok(()))
            }
            ControlRequest::RequestDiscovery {
                discovery,
                responder,
            } => {
                let fut = if discovery {
                    let stateref = state.clone();
                    Left(
                        HostDispatcher::start_discovery(state.read().host.clone()).and_then(
                            move |(mut resp, token)| {
                                stateref.write().discovery_token = token;
                                responder.send(&mut resp).into_future()
                            },
                        ),
                    )
                } else {
                    state.write().discovery_token = None;
                    Right(responder.send(&mut bt_fidl_status!()).into_future())
                };
                Output::K(fut)
            }
            ControlRequest::SetName { name, responder } => {
                let wstate = state.write();
                Output::A(
                    HostDispatcher::set_name(wstate.host.clone(), name)
                        .and_then(move |mut resp| responder.send(&mut resp)),
                )
            }
            _ => Output::B(fok(())),
        }).map(|_| ())
        .recover(|e| eprintln!("error sending response: {:?}", e))
}
