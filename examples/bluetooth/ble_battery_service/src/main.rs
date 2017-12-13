// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fuchsia_app;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate garnet_public_lib_bluetooth_fidl;
extern crate garnet_public_lib_power_fidl;
extern crate tokio_core;

mod cancelable_future;

use bt::gatt;
use bt::low_energy as le;
use garnet_public_lib_bluetooth_fidl as bt;

use cancelable_future::{Cancelable, CancelHandle};
use failure::{Error, Fail};
use fidl::{ClientEnd, FidlService, InterfacePtr};
use fuchsia_app::client::connect_to_service;
use futures::Future;
use futures::future::ok as fok;
use garnet_public_lib_power_fidl::{BatteryStatus, PowerManager, PowerManagerWatcher};
use std::cell::RefCell;
use std::collections::HashSet;
use std::fmt;
use std::rc::Rc;
use tokio_core::reactor;

const BATTERY_LEVEL_ID: u64 = 0;
const BATTERY_SERVICE_UUID: &'static str = "0000180f-0000-1000-8000-00805f9b34fb";
const BATTERY_LEVEL_UUID: &'static str = "00002A19-0000-1000-8000-00805f9b34fb";

// Name used when advertising.
const DEVICE_NAME: &'static str = "FX BLE Battery";

#[derive(Debug)]
struct BluetoothError(bt::Error);

impl fmt::Display for BluetoothError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self.0.description {
            Some(ref msg) => f.write_str(msg),
            None => write!(f, "unknown bluetooth error"),
        }
    }
}

impl Fail for BluetoothError {}

struct BatteryState {
    // The current battery percentage.
    level: u8,

    // The proxy we use to send GATT characteristic value notifications.
    service: gatt::Service_::Proxy,

    // A set of remote LE device IDs that have subscribed to battery level
    // notifications.
    configs: HashSet<String>,
}

struct BatteryService {
    state: Rc<RefCell<BatteryState>>,
}

impl BatteryService {
    pub fn new(state: Rc<RefCell<BatteryState>>) -> BatteryService {
        BatteryService { state: state }
    }
}

// GATT ServiceDelegate implementation.
impl gatt::ServiceDelegate::Server for BatteryService {
    // This is called when a remote device subscribes to battery level
    // notifications from us.
    type OnCharacteristicConfiguration = fidl::ServerImmediate<()>;
    fn on_characteristic_configuration(
        &mut self,
        _characteristic_id: u64,
        peer_id: String,
        notify: bool,
        indicate: bool,
    ) -> Self::OnCharacteristicConfiguration {
        println!(
            "Peer configured characteristic (notify: {}, indicate: {}, id: {})",
            notify,
            indicate,
            peer_id
        );

        let configs = &mut self.state.borrow_mut().configs;

        if notify {
            configs.insert(peer_id);
        } else {
            configs.remove(&peer_id);
        }

        fok(())
    }

    // This is called when a remote device requests to read the current battery
    // level.
    type OnReadValue = fidl::ServerImmediate<(Option<Vec<u8>>, gatt::ErrorCode)>;
    fn on_read_value(&mut self, _id: u64, _offset: i32) -> Self::OnReadValue {
        fok((
            Some(vec![self.state.borrow().level]),
            gatt::ErrorCode::NoError,
        ))
    }

    // The battery level characteristic that we publish below does not have the
    // "write" property, so the following delegate methods will never be called:

    type OnWriteValue = fidl::ServerImmediate<gatt::ErrorCode>;
    fn on_write_value(&mut self, _id: u64, _offset: u16, _value: Vec<u8>) -> Self::OnWriteValue {
        fok(gatt::ErrorCode::NotPermitted)
    }

    type OnWriteWithoutResponse = fidl::ServerImmediate<()>;
    fn on_write_without_response(
        &mut self,
        _id: u64,
        _offset: u16,
        _value: Vec<u8>,
    ) -> Self::OnWriteWithoutResponse {
        fok(())
    }
}

// Notifies us when the the local battery level changes.
impl PowerManagerWatcher::Server for BatteryService {
    type OnChangeBatteryStatus = fidl::ServerImmediate<()>;
    fn on_change_battery_status(
        &mut self,
        battery_status: BatteryStatus,
    ) -> Self::OnChangeBatteryStatus {
        let state = &mut self.state.borrow_mut();
        let level = battery_status.level.round() as u8;

        // Notify subscribed clients if the integer value of the battery level has changed.
        if state.level != level {
            println!("Battery percentage changed ({}%)", level);
            for peer_id in &state.configs {
                let _ = state.service.notify_value(
                    BATTERY_LEVEL_ID,
                    peer_id.clone(),
                    vec![level],
                    false,
                );
            }
        }

        state.level = level;
        fok(())
    }
}

// Start LE advertising to listen for connections. Advertising is stopped when a
// central connects and restarted when it disconnects.
fn start_advertising(state_rc: Rc<RefCell<BatteryPeripheralState>>) -> Result<(), Error> {
    println!("Listening for BLE centrals...");

    let ad = le::AdvertisingData {
        name: Some(DEVICE_NAME.to_owned()),
        tx_power_level: None,
        appearance: None,
        service_uuids: Some(vec![BATTERY_SERVICE_UUID.to_string()]),
        service_data: None,
        manufacturer_specific_data: None,
        solicited_service_uuids: None,
        uris: None,
    };

    let (delegate_local, delegate_remote) = zx::Channel::create()?;
    let delegate_ptr = InterfacePtr {
        inner: ClientEnd::new(delegate_remote),
        version: le::PeripheralDelegate::VERSION,
    };

    let mut state = state_rc.borrow_mut();
    let start_adv = state.peripheral.start_advertising(
        ad,
        None,
        Some(delegate_ptr),
        60,
        false,
    );

    state.delegate_handle = CancelHandle::new();
    let start_server =
        Cancelable::new(
            fidl::Server::new(
                le::PeripheralDelegate::Dispatcher(BatteryPeripheral { state: state_rc.clone() }),
                delegate_local,
                &state.handle,
            )?,
            &state.delegate_handle,
        );

    // Spin up the PeripheralDelegate server.
    state.handle.spawn(
        start_adv.and_then(|_| start_server).map_err(
            |e| {
                eprintln!("Failed to start advertising {:?}", e);
                ()
            },
        ),
    );
    Ok(())
}

struct BatteryPeripheralState {
    peripheral: le::Peripheral::Proxy,
    delegate_handle: CancelHandle,
    handle: reactor::Handle,
}

struct BatteryPeripheral {
    state: Rc<RefCell<BatteryPeripheralState>>,
}

impl le::PeripheralDelegate::Server for BatteryPeripheral {
    type OnCentralConnected = fidl::ServerImmediate<()>;
    fn on_central_connected(
        &mut self,
        _advertisement_id: String,
        central: le::RemoteDevice,
    ) -> Self::OnCentralConnected {
        println!("Central connected: {}", central.identifier);
        fok(())
    }

    type OnCentralDisconnected = fidl::ServerImmediate<()>;
    fn on_central_disconnected(&mut self, device_id: String) -> Self::OnCentralDisconnected {
        println!("Central disconnected: {}", device_id);

        {
            let state = self.state.borrow();
            state.delegate_handle.cancel();
        }

        if let Err(e) = start_advertising(self.state.clone()) {
            eprintln!("@@ Failed to start advertising {:?}", e);
        }

        fok(())
    }
}

fn main() {
    if let Err(e) = main_res() {
        println!("Error: {:?}", e);
    }
}

fn main_res() -> Result<(), Error> {
    let listen = match std::env::args().nth(1) {
        Some(ref flag) => {
            match flag.as_ref() {
                "--listen" => true,
                "--help" => {
                    println!("Options:\n  --listen: listen for BLE connections");
                    return Ok(());
                }
                _ => {
                    println!("invalid argument: {}", flag);
                    return Ok(());
                }
            }
        }
        None => false,
    };

    let mut core = reactor::Core::new()?;
    let handle = core.handle();

    let server = connect_to_service::<gatt::Server_::Service>(&handle)?;
    let power = connect_to_service::<PowerManager::Service>(&handle)?;

    // No security is required.
    let read_sec = Box::new(gatt::SecurityRequirements {
        encryption_required: false,
        authentication_required: false,
        authorization_required: false,
    });
    let update_sec = Box::new(gatt::SecurityRequirements {
        encryption_required: false,
        authentication_required: false,
        authorization_required: false,
    });

    // Build a GATT Battery service.
    let characteristic = gatt::Characteristic {
        id: BATTERY_LEVEL_ID,
        type_: BATTERY_LEVEL_UUID.to_string(),
        properties: vec![
            gatt::CharacteristicProperty::Read,
            gatt::CharacteristicProperty::Notify,
        ],
        permissions: Some(Box::new(gatt::AttributePermissions {
            read: Some(read_sec),
            write: None,
            update: Some(update_sec),
        })),
        descriptors: None,
    };
    let service_info = gatt::ServiceInfo {
        id: 0,
        primary: true,
        type_: BATTERY_SERVICE_UUID.to_string(),
        characteristics: Some(vec![characteristic]),
        includes: None,
    };

    // Register a power watcher to monitor the power level.
    let (power_watcher_local, power_watcher_remote) = zx::Channel::create()?;
    let watcher_ptr = InterfacePtr {
        inner: ClientEnd::new(power_watcher_remote),
        version: gatt::ServiceDelegate::VERSION,
    };
    power.watch(watcher_ptr)?;

    // Publish service and register service delegate.
    let (service_proxy, service_server) = gatt::Service_::Service::new_pair(&handle)?;
    let (delegate_local, delegate_remote) = zx::Channel::create()?;
    let delegate_ptr = InterfacePtr {
        inner: ClientEnd::new(delegate_remote),
        version: gatt::ServiceDelegate::VERSION,
    };

    let publish = server
        .publish_service(service_info, delegate_ptr, service_server)
        .map_err(|e| Error::from(e.context("Publishing service error")))
        .and_then(|status| match status.error {
            None => Ok(()),
            Some(e) => Err(Error::from(BluetoothError(*e))),
        });

    // This stores the current battery level and a list of peer device IDs that
    // have subscribed to battery level notifications.
    let state = Rc::new(RefCell::new(BatteryState {
        level: 0 as u8,
        service: service_proxy,
        configs: HashSet::new(),
    }));

    // Spin up the power watcher to start handling events.
    let power_watcher_server = fidl::Server::new(
        PowerManagerWatcher::Dispatcher(BatteryService::new(state.clone())),
        power_watcher_local,
        &handle,
    )?
        .map_err(|e| {
            Error::from(e.context("PowerManagerWatcher server error"))
        });

    // Set up the GATT service delegate.
    let service_delegate_server =
        fidl::Server::new(
            bt::ServiceDelegate::Dispatcher(BatteryService::new(state.clone())),
            delegate_local,
            &handle,
        )?
            .map_err(|e| {
                Error::from(e.context("ServiceDelegate dispatcher server error"))
            });

    // Listen for incoming connections if the user requested it. Otherwise, this
    // will simply publish the GATT service without advertising.
    if listen {
        let peripheral = connect_to_service::<le::Peripheral::Service>(&handle)?;
        let peripheral_state = Rc::new(RefCell::new(BatteryPeripheralState {
            peripheral: peripheral,
            delegate_handle: CancelHandle::new(),
            handle: handle.clone(),
        }));
        start_advertising(peripheral_state)?;
    }

    let main_fut = publish.and_then(|()| power_watcher_server.join(service_delegate_server));

    core.run(main_fut)
        .map(|((), ())| ())
}
