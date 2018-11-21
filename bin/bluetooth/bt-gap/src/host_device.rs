// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::host_dispatcher::HostDispatcher;
use crate::util::clone_host_state;
use fidl;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_bluetooth;
use fidl_fuchsia_bluetooth::Status;
use fidl_fuchsia_bluetooth_control::AdapterInfo;
use fidl_fuchsia_bluetooth_control::PairingDelegateMarker;
use fidl_fuchsia_bluetooth_control::{InputCapabilityType, OutputCapabilityType};
use fidl_fuchsia_bluetooth_gatt::ClientProxy;
use fidl_fuchsia_bluetooth_host::{BondingData, HostEvent, HostProxy};
use fidl_fuchsia_bluetooth_le::{CentralMarker, CentralProxy};
use fuchsia_async as fasync;
use fuchsia_bluetooth::bt_fidl_status;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use fuchsia_zircon as zx;
use futures::{Future, StreamExt};
use parking_lot::RwLock;
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;

pub struct HostDevice {
    pub path: PathBuf,
    host: HostProxy,
    info: AdapterInfo,
    gatt: HashMap<String, (CentralProxy, ClientProxy)>,
}

impl HostDevice {
    pub fn new(path: PathBuf, host: HostProxy, info: AdapterInfo) -> Self {
        HostDevice {
            path,
            host,
            info,
            gatt: HashMap::new(),
        }
    }

    pub fn get_host(&self) -> &HostProxy {
        &self.host
    }

    pub fn set_host_pairing_delegate(
        &self, input: InputCapabilityType, output: OutputCapabilityType,
        delegate: ClientEnd<PairingDelegateMarker>,
    ) {
        let _ = self
            .host
            .set_pairing_delegate(input, output, Some(delegate));
    }

    pub fn get_info(&self) -> &AdapterInfo {
        &self.info
    }

    pub fn store_gatt(&mut self, id: String, central: CentralProxy, client: ClientProxy) {
        // TODO(NET-1092): Use Host.Connect instead
        self.gatt.insert(id, (central, client));
    }

    pub fn rm_gatt(&mut self, id: String) -> impl Future<Output = fidl::Result<Status>> {
        let gatt_entry = self.gatt.remove(&id);
        async move {
            if let Some((central, _)) = gatt_entry {
                await!(central.disconnect_peripheral(id.as_str()))
            } else {
                Ok(bt_fidl_status!(BluetoothNotAvailable, "Unknown peripheral"))
            }
        }
    }

    pub fn set_name(&self, mut name: String) -> impl Future<Output = fidl::Result<Status>> {
        self.host.set_local_name(&mut name)
    }

    pub fn start_discovery(&mut self) -> impl Future<Output = fidl::Result<Status>> {
        self.host.start_discovery()
    }

    pub fn connect_le_central(&mut self) -> Result<CentralProxy, fidl::Error> {
        // TODO map_err
        let (service_local, service_remote) = zx::Channel::create().unwrap();
        let service_local = fasync::Channel::from_channel(service_local).unwrap();
        let server = fidl::endpoints::ServerEnd::<CentralMarker>::new(service_remote);
        self.host.request_low_energy_central(server)?;
        let proxy = CentralProxy::new(service_local);
        Ok(proxy)
    }

    pub fn close(&self) -> Result<(), fidl::Error> {
        self.host.close()
    }

    pub fn restore_bonds(&self, mut bonds: Vec<BondingData>) -> impl Future<Output = fidl::Result<Status>> {
        self.host.add_bonded_devices(&mut bonds.iter_mut())
    }

    pub fn set_connectable(&self, value: bool) -> impl Future<Output = fidl::Result<Status>> {
        self.host.set_connectable(value)
    }

    pub fn stop_discovery(&self) -> impl Future<Output = fidl::Result<Status>> {
        self.host.stop_discovery()
    }

    pub fn set_discoverable(&self, discoverable: bool) -> impl Future<Output = fidl::Result<Status>> {
        self.host.set_discoverable(discoverable)
    }

    pub fn enable_background_scan(&self, enable: bool) -> fidl::Result<()> {
        self.host.enable_background_scan(enable)
    }
}

pub async fn run(hd: HostDispatcher, host: Arc<RwLock<HostDevice>>) -> fidl::Result<()> {
    let mut stream = host.read().host.take_event_stream();

    while let Some(event) = await!(stream.next()) {
        let host_ = host.clone();
        let dispatcher = hd.clone();
        match event? {
            HostEvent::OnAdapterStateChanged { ref state } => {
                host_.write().info.state = Some(Box::new(clone_host_state(&state)));
            }
            // TODO(NET-968): Add integration test for this.
            HostEvent::OnDeviceUpdated { device } => hd.on_device_updated(device),
            // TODO(NET-1038): Add integration test for this.
            HostEvent::OnDeviceRemoved { identifier } => hd.on_device_removed(identifier),
            HostEvent::OnNewBondingData { data } => {
                fx_log_info!("Received bonding data");
                if let Err(e) = dispatcher.store_bond(data) {
                    fx_log_err!("Failed to persist bonding data: {:#?}", e);
                }
            }
        };
    };
    Ok(())
}
