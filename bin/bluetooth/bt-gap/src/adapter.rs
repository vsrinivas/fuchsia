// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl;
use fidl::endpoints2::ServerEnd;
use fidl_bluetooth::Status;
use fidl_bluetooth_control::AdapterInfo;
use fidl_bluetooth_host::{AdapterDelegate, AdapterDelegateImpl, AdapterDelegateMarker,
                          AdapterMarker, AdapterProxy, HostProxy};
use futures::Future;
use futures::future::ok as fok;
use host_dispatcher::HostDispatcher;
use parking_lot::{Mutex, RwLock};
use std::sync::Arc;

type HostAdapterPtr = ServerEnd<AdapterMarker>;
type AdapterDelegatePtr = ServerEnd<AdapterDelegateMarker>;

pub struct HostDevice {
    host: HostProxy,
    adapter: AdapterProxy,
    info: AdapterInfo,
}

impl HostDevice {
    pub fn new(host: HostProxy, adapter: AdapterProxy, info: AdapterInfo) -> Self {
        HostDevice {
            host,
            adapter,
            info,
        }
    }

    pub fn get_host(&self) -> &HostProxy {
        &self.host
    }
    pub fn get_info(&self) -> &AdapterInfo {
        &self.info
    }

    pub fn set_name(&self, mut name: String) -> impl Future<Item = Status, Error = fidl::Error> {
        self.adapter.set_local_name(&mut name)
    }

    pub fn start_discovery(&mut self) -> impl Future<Item = Status, Error = fidl::Error> {
        self.adapter.start_discovery()
    }

    pub fn close(&self) -> Result<(), fidl::Error> {
        self.host.close()
    }

    pub fn stop_discovery(&self) -> impl Future<Item = Status, Error = fidl::Error> {
        self.adapter.stop_discovery()
    }

    pub fn set_discoverable(
        &mut self, discoverable: bool
    ) -> impl Future<Item = Status, Error = fidl::Error> {
        self.adapter.set_discoverable(discoverable)
    }
}

pub fn run_host_device(
    hd: Arc<RwLock<HostDispatcher>>, adapter: Arc<Mutex<HostDevice>>
) -> impl AdapterDelegate {
    AdapterDelegateImpl {
        state: (hd, adapter),
        on_open: |_, _| fok(()),
        on_device_discovered: |(hd, adapter), mut remote_device, _res| {
            if let Some(ref delegate) = hd.write().remote_device_delegate {
                delegate
                    .on_device_updated(&mut remote_device)
                    .map_err(|e| warn!("Device Updated Callback Error: {:?}", e));
            }
            fok(())
        },
        on_adapter_state_changed: |(_hd, adapter), adapter_state, _res| {
            let mut adapter = adapter.lock();
            adapter.info.state = Some(Box::new(adapter_state));
            fok(())
        },
    }
}
