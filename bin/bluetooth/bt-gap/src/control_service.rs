// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use fidl::encoding2::OutOfLine;
use fidl_fuchsia_bluetooth;
use fidl_fuchsia_bluetooth_control::{Control, ControlImpl};
use futures::{future, Future, FutureExt, Never};
use futures::future::Either::{Left, Right};
use futures::prelude::*;
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
    hd: Arc<RwLock<HostDispatcher>>, chan: async::Channel
) -> impl Future<Item = (), Error = Never> {
    ControlImpl {
        state: Arc::new(RwLock::new(ControlServiceState {
            host: hd,
            discovery_token: None,
            discoverable_token: None,
        })),
        on_open: |state, handle| {
            let wstate = state.write();
            let mut hd = wstate.host.write();
            hd.event_listeners.push(handle.clone());
            future::ok(())
        },
        connect: |_, _, res| {
            res.send(&mut bt_fidl_status!(NotSupported))
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        set_io_capabilities: |_, _, _, _res| {
            //TODO(bwb): Implement this method
            future::ok(())
        },
        disconnect: |_, _, res| {
            res.send(&mut bt_fidl_status!(NotSupported))
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        forget: |_, _, res| {
            res.send(&mut bt_fidl_status!(NotSupported))
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        set_name: |state, name, res| {
            let wstate = state.write();
            HostDispatcher::set_name(wstate.host.clone(), name)
                .and_then(move |mut resp| res.send(&mut resp))
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        get_active_adapter_info: |state, res| {
            let wstate = state.write();
            let mut hd = wstate.host.write();
            let mut adap = hd.get_active_adapter_info();

            res.send(adap.as_mut().map(OutOfLine))
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        get_known_remote_devices: |_state, _res| future::ok(()),
        get_adapters: |state, res| {
            let wstate = state.write();
            let mut hd = wstate.host.clone();
            HostDispatcher::get_adapters(&mut hd)
                .and_then(move |mut resp| res.send(Some(&mut resp.iter_mut())))
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        is_bluetooth_available: |state, res| {
            let rstate = state.read();
            let mut hd = rstate.host.write();
            let is_available = hd.get_active_adapter_info().is_some();
            res.send(is_available)
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        request_discovery: |state, discover, res| {
            let fut = if discover {
                let stateref = state.clone();
                Left(
                    HostDispatcher::start_discovery(state.read().host.clone()).and_then(
                        move |(mut resp, token)| {
                            stateref.write().discovery_token = token;
                            res.send(&mut resp).into_future()
                        },
                    ),
                )
            } else {
                state.write().discovery_token = None;
                Right(res.send(&mut bt_fidl_status!()).into_future())
            };
            fut.recover(|e| eprintln!("error sending response: {:?}", e))
        },
        set_discoverable: |state, discoverable, res| {
            let fut = if discoverable {
                let stateref = state.clone();
                Left(
                    HostDispatcher::set_discoverable(state.read().host.clone()).and_then(
                        move |(mut resp, token)| {
                            stateref.write().discoverable_token = token;
                            res.send(&mut resp).into_future()
                        },
                    ),
                )
            } else {
                state.write().discoverable_token = None;
                Right(res.send(&mut bt_fidl_status!()).into_future())
            };
            fut.recover(|e| eprintln!("error sending response: {:?}", e))
        },
        set_active_adapter: |state, adapter, res| {
            let wstate = state.write();
            let mut success = wstate.host.write().set_active_adapter(adapter.clone());
            res.send(&mut success)
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        set_pairing_delegate: |state, delegate, _res| {
            let mut wstate = state.write();
            if let Some(delegate) = delegate {
                if let Ok(proxy) = delegate.into_proxy() {
                    wstate.host.write().pairing_delegate = Some(proxy);
                }
            } else {
                wstate.host.write().pairing_delegate = None;
            }
            future::ok(())
        },
    }.serve(chan)
        .recover(|e| eprintln!("error running service: {:?}", e))
}
