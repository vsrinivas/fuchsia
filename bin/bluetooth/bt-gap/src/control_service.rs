// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use fidl::encoding2::OutOfLine;
use fidl::endpoints2::RequestStream;
use fidl_fuchsia_bluetooth;
use fidl_fuchsia_bluetooth_control::{ControlRequest, ControlRequestStream};
use async::temp::Either::{Left, Right};
use futures::prelude::*;
use failure::Error;
use futures::{future, Future, FutureExt};
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
) -> impl Future< Output = Result<(), Error>> {
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
    unsafe_many_futures!(Output, [A, B, C, D, E, F, G, H, I, J, K, L, M]);

    stream
        .try_for_each(move |evt| match evt {
            ControlRequest::Connect {
                device_id: _,
                responder,
            } => {
                let _ = responder.send(&mut bt_fidl_status!(NotSupported));
                Output::B(future::ready(Ok(())))
            }
            ControlRequest::SetDiscoverable {
                discoverable,
                responder,
            } => {
                let fut = if discoverable {
                    let stateref = state.clone();
                    Left(HostDispatcher::set_discoverable(state.read().host.clone()).and_then(
                            move |(mut resp, token)| {
                                stateref.write().discoverable_token = token;
                                future::ready(responder.send(&mut resp))
                            },
                        )
                    )
                } else {
                    state.write().discoverable_token = None;
                    Right(future::ready(responder.send(&mut bt_fidl_status!())))
                };
                Output::C(fut)
            }
            ControlRequest::SetIoCapabilities { .. } => Output::E(future::ready(Ok(()))),
            ControlRequest::Forget { device_id: _, responder } => {
                let _ = responder.send(&mut bt_fidl_status!(NotSupported));
                Output::L(future::ready(Ok(())))
            },
            ControlRequest::Disconnect { device_id: _, responder } => {
                let _ = responder.send(&mut bt_fidl_status!(NotSupported));
                Output::M(future::ready(Ok(())))
            },
            ControlRequest::GetKnownRemoteDevices { .. } => Output::K(future::ready(Ok(()))),
            ControlRequest::IsBluetoothAvailable { responder } => {
                let rstate = state.read();
                let mut hd = rstate.host.write();
                let is_available = hd.get_active_adapter_info().is_some();
                let _ = responder.send(is_available);
                Output::F(future::ready(Ok(())))
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
                Output::D(future::ready(Ok(())))
            }
            ControlRequest::GetAdapters { responder } => {
                let wstate = state.write();
                let mut hd = wstate.host.clone();
                let fut = HostDispatcher::get_adapters(&mut hd)
                    .map_ok(move |mut resp| responder.send(Some(&mut resp.iter_mut()))).map(|_| Ok(()));
                Output::H(fut)
            }
            ControlRequest::SetActiveAdapter {
                identifier,
                responder,
            } => {
                let wstate = state.write();
                let mut success = wstate.host.write().set_active_adapter(identifier.clone());
                let _ = responder.send(&mut success);
                Output::I(future::ready(Ok(())))
            }
            ControlRequest::GetActiveAdapterInfo { responder } => {
                let wstate = state.write();
                let mut hd = wstate.host.write();
                let mut adap = hd.get_active_adapter_info();

                let _ = responder.send(adap.as_mut().map(OutOfLine));
                Output::J(future::ready(Ok(())))
            }
            ControlRequest::RequestDiscovery {
                discovery,
                responder,
            } => {
                let fut = if discovery {
                    let stateref = state.clone();
                    Left(
                        HostDispatcher::start_discovery(state.read().host.clone()).map_ok(
                            move |(mut resp, token)| {
                                stateref.write().discovery_token = token;
                                responder.send(&mut resp)
                            },
                        ).map(|_| Ok(()))
                    )
                } else {
                    state.write().discovery_token = None;
                    Right(future::ready(responder.send(&mut bt_fidl_status!())))
                };
                Output::G(fut)
            },
            ControlRequest::SetName { name, responder } => {
                let wstate = state.write();
                Output::A(
                    HostDispatcher::set_name(wstate.host.clone(), name)
                        .and_then(move |mut resp| future::ready(Ok(responder.send(&mut resp)))).map(|_| Ok(())),
                )
            }
        }).map(|_| Ok(()))
}
