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
use fuchsia_async::{self as fasync,
                    temp::Either::{Left, Right}};
use fuchsia_bluetooth::{bt_fidl_status, make_clones};
use fuchsia_syslog::{fx_log, fx_log_err, fx_log_info};
use fuchsia_zircon as zx;
use futures::TryFutureExt;
use futures::TryStreamExt;
use futures::{future, Future};
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

    pub fn rm_gatt(&mut self, id: String) -> impl Future<Output = Result<Status, fidl::Error>> {
        if let Some((central, _)) = self.gatt.remove(&id) {
            Left(
                central
                    .disconnect_peripheral(id.as_str())
                    .and_then(|res| future::ready(Ok(res))),
            )
        } else {
            Right(future::ready(Ok(bt_fidl_status!(
                BluetoothNotAvailable,
                "Unknown peripheral"
            ))))
        }
    }

    pub fn set_name(&self, mut name: String) -> impl Future<Output = Result<Status, fidl::Error>> {
        self.host.set_local_name(&mut name)
    }

    pub fn start_discovery(&mut self) -> impl Future<Output = Result<Status, fidl::Error>> {
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

    pub fn restore_bonds(
        &self, mut bonds: Vec<BondingData>,
    ) -> impl Future<Output = Result<Status, fidl::Error>> {
        self.host.add_bonded_devices(&mut bonds.iter_mut())
    }

    pub fn set_connectable(&self, value: bool) -> impl Future<Output = Result<Status, fidl::Error>> {
        self.host.set_connectable(value)
    }

    pub fn stop_discovery(&self) -> impl Future<Output = Result<Status, fidl::Error>> {
        self.host.stop_discovery()
    }

    pub fn set_discoverable(
        &self, discoverable: bool,
    ) -> impl Future<Output = Result<Status, fidl::Error>> {
        self.host.set_discoverable(discoverable)
    }

    pub fn enable_background_scan(&self, enable: bool) -> Result<(), fidl::Error> {
        self.host.enable_background_scan(enable)
    }
}

pub fn run(
    hd: Arc<RwLock<HostDispatcher>>, host: Arc<RwLock<HostDevice>>,
) -> impl Future<Output = Result<(), fidl::Error>> {
    make_clones!(host => host_stream, host);
    let stream = host_stream.read().host.take_event_stream();
    stream.try_for_each(move |evt| {
        match evt {
            HostEvent::OnAdapterStateChanged { ref state } => {
                host.write().info.state = Some(Box::new(clone_host_state(&state)));
            }
            // TODO(NET-968): Add integration test for this.
            HostEvent::OnDeviceUpdated { mut device } => {
                // TODO(NET-1297): generic method for this pattern
                for listener in hd.read().event_listeners.iter() {
                    let _res = listener
                        .send_on_device_updated(&mut device)
                        .map_err(|e| fx_log_err!("Failed to send device updated event: {:?}", e));
                }
            }
            // TODO(NET-1038): Add integration test for this.
            HostEvent::OnDeviceRemoved { identifier } => {
                for listener in hd.read().event_listeners.iter() {
                    let _res = listener
                        .send_on_device_removed(&identifier)
                        .map_err(|e| fx_log_err!("Failed to send device removed event: {:?}", e));
                }
            }
            HostEvent::OnNewBondingData { data } => {
                fx_log_info!("Received bonding data");
                if let Err(e) = hd.write().stash.store_bond(data) {
                    fx_log_err!("Failed to persist bonding data: {:#?}", e);
                }
            }
        };
        future::ready(Ok(()))
    })
}
