// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_bluetooth_control::{
        AdapterInfo, DeviceClass, InputCapabilityType, OutputCapabilityType, PairingDelegateMarker,
        RemoteDevice,
    },
    fidl_fuchsia_bluetooth_gatt::ClientProxy,
    fidl_fuchsia_bluetooth_host::{BondingData, HostData, HostEvent, HostProxy},
    fidl_fuchsia_bluetooth_le::CentralProxy,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::{Future, FutureExt, StreamExt},
    parking_lot::RwLock,
    std::collections::HashMap,
    std::path::PathBuf,
    std::sync::Arc,
};

use crate::{
    types::{self, from_fidl_status, Error},
    util::clone_host_state,
};

pub struct HostDevice {
    pub path: PathBuf,
    host: HostProxy,
    info: AdapterInfo,
    gatt: HashMap<String, (CentralProxy, ClientProxy)>,
}

// Many HostDevice methods return impl Future rather than being implemented as `async`. This has an
// important behavioral difference in that the function body is triggered immediately when called.
//
// If they were instead declared async, the function body would not be executed until the first time
// the future was polled.
impl HostDevice {
    pub fn new(path: PathBuf, host: HostProxy, info: AdapterInfo) -> Self {
        HostDevice { path, host, info, gatt: HashMap::new() }
    }

    pub fn get_host(&self) -> &HostProxy {
        &self.host
    }

    pub fn set_host_pairing_delegate(
        &self,
        input: InputCapabilityType,
        output: OutputCapabilityType,
        delegate: ClientEnd<PairingDelegateMarker>,
    ) {
        let _ = self.host.set_pairing_delegate(input, output, Some(delegate));
    }

    pub fn get_info(&self) -> &AdapterInfo {
        &self.info
    }

    pub fn rm_gatt(&mut self, id: String) -> impl Future<Output = types::Result<()>> {
        let gatt_entry = self.gatt.remove(&id);
        async move {
            if let Some((central, _)) = gatt_entry {
                from_fidl_status(await!(central.disconnect_peripheral(id.as_str())))
            } else {
                Err(Error::not_found("Unknown Peripheral"))
            }
        }
    }

    pub fn set_name(&self, mut name: String) -> impl Future<Output = types::Result<()>> {
        self.host.set_local_name(&mut name).map(from_fidl_status)
    }

    pub fn set_device_class(
        &self,
        mut cod: DeviceClass,
    ) -> impl Future<Output = types::Result<()>> {
        self.host.set_device_class(&mut cod).map(from_fidl_status)
    }

    pub fn start_discovery(&mut self) -> impl Future<Output = types::Result<()>> {
        self.host.start_discovery().map(from_fidl_status)
    }

    pub fn connect(&mut self, device_id: String) -> impl Future<Output = types::Result<()>> {
        self.host.connect(&device_id).map(from_fidl_status)
    }

    pub fn forget(&mut self, peer_id: String) -> impl Future<Output = types::Result<()>> {
        self.host.forget(&peer_id).map(from_fidl_status)
    }

    pub fn close(&self) -> types::Result<()> {
        self.host.close().map_err(|e| e.into())
    }

    pub fn restore_bonds(
        &self,
        mut bonds: Vec<BondingData>,
    ) -> impl Future<Output = types::Result<()>> {
        self.host.add_bonded_devices(&mut bonds.iter_mut()).map(from_fidl_status)
    }

    pub fn set_connectable(&self, value: bool) -> impl Future<Output = types::Result<()>> {
        self.host.set_connectable(value).map(from_fidl_status)
    }

    pub fn stop_discovery(&self) -> impl Future<Output = types::Result<()>> {
        self.host.stop_discovery().map(from_fidl_status)
    }

    pub fn set_discoverable(&self, discoverable: bool) -> impl Future<Output = types::Result<()>> {
        self.host.set_discoverable(discoverable).map(from_fidl_status)
    }

    pub fn set_local_data(&self, mut data: HostData) -> types::Result<()> {
        self.host.set_local_data(&mut data)?;
        Ok(())
    }

    pub fn enable_privacy(&self, enable: bool) -> types::Result<()> {
        self.host.enable_privacy(enable).map_err(Error::from)
    }

    pub fn enable_background_scan(&self, enable: bool) -> types::Result<()> {
        self.host.enable_background_scan(enable).map_err(Error::from)
    }
}

pub trait HostListener {
    fn on_peer_updated(&mut self, device: RemoteDevice);
    fn on_peer_removed(&mut self, identifier: String);
    fn on_new_host_bond(&mut self, data: BondingData) -> Result<(), failure::Error>;
}

pub async fn handle_events<H: HostListener>(
    mut listener: H,
    host: Arc<RwLock<HostDevice>>,
) -> types::Result<()> {
    let mut stream = host.read().host.take_event_stream();

    while let Some(event) = await!(stream.next()) {
        let host_ = host.clone();
        match event? {
            HostEvent::OnAdapterStateChanged { ref state } => {
                host_.write().info.state = Some(Box::new(clone_host_state(&state)));
            }
            // TODO(NET-968): Add integration test for this.
            HostEvent::OnDeviceUpdated { device } => listener.on_peer_updated(device),
            // TODO(NET-1038): Add integration test for this.
            HostEvent::OnDeviceRemoved { identifier } => listener.on_peer_removed(identifier),
            HostEvent::OnNewBondingData { data } => {
                fx_log_info!("Received bonding data");
                if let Err(e) = listener.on_new_host_bond(data) {
                    fx_log_err!("Failed to persist bonding data: {:#?}", e);
                }
            }
        };
    }
    Ok(())
}
