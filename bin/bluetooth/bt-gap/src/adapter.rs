// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl;
use fidl_bluetooth::Status;
use fidl_bluetooth_control::AdapterInfo;
use fidl_bluetooth_host::{AdapterEvent, AdapterEventStream, AdapterProxy, HostProxy};
use futures::{FutureExt, StreamExt};
use futures::Future;
use futures::future::ok as fok;
use host_dispatcher::HostDispatcher;
use parking_lot::RwLock;
use std::sync::Arc;
use util::clone_adapter_state;

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
    hd: Arc<RwLock<HostDispatcher>>, adapter: Arc<RwLock<HostDevice>>
) -> impl Future<Item = AdapterEventStream, Error = fidl::Error> {
    make_clones!(adapter => stream_adapter, adapter);
    let stream = stream_adapter.read().adapter.take_event_stream();
    stream
        .for_each(move |evt| {
            match evt {
                AdapterEvent::OnAdapterStateChanged { ref state } => {
                    adapter.write().info.state = Some(Box::new(clone_adapter_state(&state)));
                }
                AdapterEvent::OnDeviceDiscovered { mut device } => {
                    if let Some(ref events) = hd.write().events {
                        let _res = events
                            .send_on_device_updated(&mut device)
                            .map_err(|e| error!("Failed to send device discovery event: {:?}", e));
                    }
                }
            };
            fok(())
        })
        .err_into()
}
