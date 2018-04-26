// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use fidl_bluetooth;
use fidl_bluetooth_control::{Control, ControlImpl};
use futures::{Future, FutureExt, Never};
use futures::future;
use futures::prelude::*;
use host_dispatcher::*;
use parking_lot::RwLock;
use std::sync::Arc;
#[macro_use]
use util;

/// Build the ControlImpl to interact with fidl messages
/// State is stored in the HostDispatcher object
pub fn make_control_service(
    hd: Arc<RwLock<HostDispatcher>>, chan: async::Channel
) -> impl Future<Item = (), Error = Never> {
    ControlImpl {
        state: hd,
        connect: |_, _, _, res| {
            res.send(&mut bt_fidl_status!(NotSupported))
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
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
        set_discoverable: |_hd, _is_discoverable, res| {
            res.send(&mut bt_fidl_status!(NotSupported))
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        set_name: |hd, name, res| {
            let mut hd = hd.write();
            hd.set_name(name);
            res.send(&mut bt_fidl_status!())
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        get_active_adapter_info: |hd, res| {
            let mut hd = hd.read();
            let adap = hd.get_active_adapter_info();

            res.send(&mut adap.map(|a| Box::new(a)))
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        get_adapters: |hd, res| {
            //TODO:(NET-854)
            let hd = hd.read();
            let mut adapters = vec![];
            for adapter in hd.get_adapters() {
                adapters.push(adapter);
            }
            res.send(&mut Some(adapters))
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        get_known_remote_devices: |_state, _res| future::ok(()),
        is_bluetooth_available: |hd, res| {
            let hd = hd.read();
            let mut is_avail = if hd.get_adapters().len() > 0 {
                true
            } else {
                false
            };
            res.send(&mut is_avail)
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        request_discovery: |hd, discover, res| {
            let mut hd = hd.write();
            if discover {
                hd.start_discovery()
                    .and_then(move |mut resp| res.send(&mut resp))
                    .recover(|e| eprintln!("error sending response: {:?}", e))
                    .left_future()
            } else {
                hd.stop_discovery()
                    .and_then(move |mut resp| res.send(&mut resp))
                    .recover(|e| eprintln!("error sending response: {:?}", e))
                    .right_future()
            }
        },
        set_active_adapter: |hd, adapter, res| {
            let mut success = hd.write().set_active_adapter(adapter.clone());
            res.send(&mut success)
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        set_delegate: |hd, delegate, _res| {
            if let Ok(proxy) = delegate.into_proxy() {
                hd.write().control_delegate = Some(proxy);
            }
            future::ok(())
        },
        set_pairing_delegate: |hd, _, _, delegate, _res| {
            if let Some(delegate) = delegate {
                if let Ok(proxy) = delegate.into_proxy() {
                    hd.write().pairing_delegate = Some(proxy);
                }
            }
            future::ok(())
        },
        set_remote_device_delegate: |hd, remote_device_delegate, _include_rssi, _res| {
            if let Some(rdd) = remote_device_delegate {
                if let Ok(proxy) = rdd.into_proxy() {
                    hd.write().remote_device_delegate = Some(proxy);
                }
            }
            future::ok(())
        },
    }.serve(chan)
        .recover(|e| eprintln!("error running service: {:?}", e))
}
