// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_bluetooth::DeviceClass,
    fidl_fuchsia_bluetooth::PeerId as FidlPeerId,
    fidl_fuchsia_bluetooth_control::{
        self as control, HostData, InputCapabilityType, OutputCapabilityType,
        PairingDelegateMarker, PairingOptions,
    },
    fidl_fuchsia_bluetooth_gatt::ClientProxy,
    fidl_fuchsia_bluetooth_host::{HostEvent, HostProxy},
    fidl_fuchsia_bluetooth_le::CentralProxy,
    fuchsia_bluetooth::{
        inspect::Inspectable,
        types::{BondingData, HostInfo, Peer, PeerId},
    },
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::{Future, FutureExt, StreamExt},
    parking_lot::RwLock,
    pin_utils::pin_mut,
    std::{collections::HashMap, convert::TryInto, path::PathBuf, sync::Arc},
};

use crate::types::{self, from_fidl_status, Error};

pub struct HostDevice {
    pub path: PathBuf,
    host: HostProxy,
    info: Inspectable<HostInfo>,
    gatt: HashMap<String, (CentralProxy, ClientProxy)>,
}

// Many HostDevice methods return impl Future rather than being implemented as `async`. This has an
// important behavioral difference in that the function body is triggered immediately when called.
//
// If they were instead declared async, the function body would not be executed until the first time
// the future was polled.
impl HostDevice {
    pub fn new(path: PathBuf, host: HostProxy, info: Inspectable<HostInfo>) -> Self {
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

    pub fn get_info(&self) -> &HostInfo {
        &self.info
    }

    pub fn rm_gatt(&mut self, id: String) -> impl Future<Output = types::Result<()>> {
        let gatt_entry = self.gatt.remove(&id);
        async move {
            if let Some((central, _)) = gatt_entry {
                from_fidl_status(central.disconnect_peripheral(id.as_str()).await)
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

    pub fn disconnect(&mut self, device_id: String) -> impl Future<Output = types::Result<()>> {
        self.host.disconnect(&device_id).map(from_fidl_status)
    }

    pub fn pair(
        &mut self,
        mut id: FidlPeerId,
        options: PairingOptions,
    ) -> impl Future<Output = types::Result<()>> {
        self.host.pair(&mut id, options).map(from_fidl_status)
    }

    pub fn forget(&mut self, peer_id: String) -> impl Future<Output = types::Result<()>> {
        self.host.forget(&peer_id).map(from_fidl_status)
    }

    pub fn close(&self) -> types::Result<()> {
        self.host.close().map_err(|e| e.into())
    }

    pub fn restore_bonds(
        &self,
        bonds: Vec<BondingData>,
    ) -> impl Future<Output = types::Result<()>> {
        let mut bonds: Vec<_> = bonds.into_iter().map(control::BondingData::from).collect();
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
    fn on_peer_updated(&mut self, peer: Peer);
    fn on_peer_removed(&mut self, id: PeerId);

    type HostBondFut: Future<Output = Result<(), anyhow::Error>>;
    fn on_new_host_bond(&mut self, data: BondingData) -> Self::HostBondFut;
}

// TODO(armansito): It feels odd to expose it only so it is available to test/host_device.rs. It
// might be better to move the host_device tests into this module.
pub async fn refresh_host_info(host: Arc<RwLock<HostDevice>>) -> types::Result<()> {
    let proxy = host.read().host.clone();
    let info = proxy.watch_state().await?;
    host.write().info.update(info.try_into()?);
    Ok(())
}

/// Monitors updates from a bt-host device and notifies `listener`. The returned Future represents
/// a task that never ends in successful operation and only returns in case of a failure to
/// communicate with the bt-host device.
pub async fn watch_events<H: HostListener + Clone>(
    listener: H,
    host: Arc<RwLock<HostDevice>>,
) -> types::Result<()> {
    let handle_fidl = handle_fidl_events(listener.clone(), host.clone());
    let watch_peers = watch_peers(listener, host.clone());
    let watch_state = watch_state(host);
    pin_mut!(handle_fidl);
    pin_mut!(watch_peers);
    pin_mut!(watch_state);
    futures::select! {
        res1 = handle_fidl.fuse() => res1,
        res2 = watch_peers.fuse() => res2,
        res3 = watch_state.fuse() => res3,
    }
}

async fn handle_fidl_events<H: HostListener>(
    mut listener: H,
    host: Arc<RwLock<HostDevice>>,
) -> types::Result<()> {
    let mut stream = host.read().host.take_event_stream();
    while let Some(event) = stream.next().await {
        match event? {
            HostEvent::OnNewBondingData { data } => {
                fx_log_info!("Received bonding data");
                let data: BondingData = match data.try_into() {
                    Err(e) => {
                        fx_log_err!("Invalid bonding data, ignoring: {:#?}", e);
                        continue;
                    }
                    Ok(data) => data,
                };
                if let Err(e) = listener.on_new_host_bond(data.into()).await {
                    fx_log_err!("Failed to persist bonding data: {:#?}", e);
                }
            }
        };
    }
    Err(types::Error::InternalError(format_err!("Host FIDL event stream terminated")))
}

async fn watch_peers<H: HostListener + Clone>(
    mut listener: H,
    host: Arc<RwLock<HostDevice>>,
) -> types::Result<()> {
    let proxy = host.read().host.clone();
    loop {
        let (updated, removed) = proxy.watch_peers().await?;
        for peer in updated.into_iter() {
            listener.on_peer_updated(peer.try_into()?);
        }
        for id in removed.into_iter() {
            listener.on_peer_removed(id.into());
        }
    }
}

async fn watch_state(host: Arc<RwLock<HostDevice>>) -> types::Result<()> {
    loop {
        refresh_host_info(host.clone()).await?;
    }
}
