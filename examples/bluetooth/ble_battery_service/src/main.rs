// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(conservative_impl_trait)]
#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate fidl_bluetooth as bt;
extern crate fidl_bluetooth_gatt as gatt;
extern crate fidl_bluetooth_low_energy as le;
extern crate fidl_power_manager;
extern crate parking_lot;

mod cancelable_future;

use component::client::connect_to_service;
use cancelable_future::{Cancelable, CancelHandle};
use failure::{Error, Fail};
use futures::prelude::*;
use futures::future::{FutureResult, ok as fok};
use fidl_power_manager::{
    PowerManager,
    PowerManagerMarker,
    PowerManagerWatcher,
    PowerManagerWatcherImpl,
    PowerManagerWatcherMarker,
};
use gatt::ServiceDelegate;
use le::PeripheralDelegate;
use parking_lot::Mutex;
use std::collections::HashSet;
use std::fmt;
use std::sync::Arc;

const BATTERY_LEVEL_ID: u64 = 0;
const BATTERY_SERVICE_UUID: &'static str = "0000180f-0000-1000-8000-00805f9b34fb";
const BATTERY_LEVEL_UUID: &'static str = "00002A19-0000-1000-8000-00805f9b34fb";

// Name used when advertising.
const DEVICE_NAME: &'static str = "FX BLE Battery";

// TODO(TO-930): extract this somewhere common
fn catch_and_log_err<F>(ctx: &'static str, f: F) -> FutureResult<(), Never>
    where F: FnOnce() -> Result<(), fidl::Error>
{
    let res = f();
    if let Err(e) = res {
        eprintln!("Error running fidl handler {}: {:?}", ctx, e);
    }
    fok(())
}

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
    service: gatt::Service_Proxy,

    // A set of remote LE device IDs that have subscribed to battery level
    // notifications.
    configs: HashSet<String>,
}

fn gatt_service_delegate(state: Arc<Mutex<BatteryState>>, channel: async::Channel)
    -> impl Future<Item = (), Error = Never>
{
    gatt::ServiceDelegateImpl {
        state,
        on_characteristic_configuration: |state, _, peer_id, notify, indicate, _|
            catch_and_log_err("on_characteristic_configuration", || {
                println!(
                    "Peer configured characteristic (notify: {}, indicate: {}, id: {})",
                    notify,
                    indicate,
                    peer_id
                    );

                let configs = &mut state.lock().configs;

                if notify {
                    configs.insert(peer_id);
                } else {
                    configs.remove(&peer_id);
                }

                Ok(())
            }
        ),
        on_read_value: |state, _, _, resp| catch_and_log_err("on_read_value", || {
            resp.send(
                &mut Some(vec![state.lock().level]),
                &mut gatt::ErrorCode::NoError,
            )
        }),
        on_write_value: |_state, _, _, _, resp| catch_and_log_err("on_write_value", || {
            resp.send(&mut gatt::ErrorCode::NotPermitted)
        }),
        on_write_without_response: |_state, _, _, _, resp| catch_and_log_err("on_write_without_response", || {
            Ok(())
        }),
    }
    .serve(channel)
    .recover(|e| eprintln!("error running gatt service delegate: {:?}", e))
}

fn power_manager_watcher(state: Arc<Mutex<BatteryState>>, channel: async::Channel)
    -> impl Future<Item = (), Error = Never>
{
    PowerManagerWatcherImpl {
        state,
        on_change_battery_status: |state, battery_status, _|
            catch_and_log_err("on_change_battery_status", || {
                let state = &mut state.lock();
                let level = battery_status.level.round() as u8;

                // Notify subscribed clients if the integer value of the battery level has changed.
                if state.level != level {
                    println!("Battery percentage changed ({}%)", level);
                    for peer_id in &state.configs {
                        let _ = state.service.notify_value(
                            &mut BATTERY_LEVEL_ID,
                            &mut peer_id.clone(),
                            &mut vec![level],
                            &mut false,
                            );
                    }
                }

                state.level = level;
                Ok(())
            }
        ),
    }
    .serve(channel)
    .recover(|e| eprintln!("error running power manager watcher: {:?}", e))
}

// Start LE advertising to listen for connections. Advertising is stopped when a
// central connects and restarted when it disconnects.
fn start_advertising(state_rc: Arc<Mutex<BatteryPeripheralState>>) -> Result<(), Error> {
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
    let delegate_local = async::Channel::from_channel(delegate_local)?;
    let delegate_ptr = fidl::endpoints2::ClientEnd::<le::PeripheralDelegateMarker>::new(delegate_remote);

    let mut state = state_rc.lock();
    let start_adv = state.peripheral.start_advertising(
        &mut ad,
        &mut None,
        &mut Some(delegate_ptr),
        &mut 60,
        &mut false,
    );

    state.delegate_handle = CancelHandle::new();
    let start_server =
        Cancelable::new(
            peripheral_delegate(state_rc.clone(), delegate_local),
            &state.delegate_handle,
        );

    // Spin up the PeripheralDelegate server.
    async::spawn(start_adv.and_then(|_| start_server).recover(
        |e| eprintln!("Failed to start advertising {:?}", e)));

    Ok(())
}

struct BatteryPeripheralState {
    peripheral: le::PeripheralProxy,
    delegate_handle: CancelHandle,
}

fn peripheral_delegate(state: Arc<Mutex<BatteryPeripheralState>>, channel: async::Channel)
    -> impl Future<Item = (), Error = Never>
{
    le::PeripheralDelegateImpl {
        state,
        on_central_connected: |_state, _, central, _| catch_and_log_err("on_central_connected", || {
            println!("Central connected: {}", central.identifier);
            Ok(())
        }),
        on_central_disconnected: |state, device_id, _| catch_and_log_err("on_central_disconnected", || {
            println!("Central disconnected: {}", device_id);

            {
                let state = state.lock();
                state.delegate_handle.cancel();
            }

            if let Err(e) = start_advertising(state.clone()) {
                eprintln!("@@ Failed to start advertising {:?}", e);
            }

            Ok(())
        }),
    }
    .serve(channel)
    .recover(|e| eprintln!("error running peripheral delegate: {:?}", e))
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

    let mut exec = async::Executor::new()?;

    let server = connect_to_service::<gatt::Server_Marker>()?;
    let power = connect_to_service::<PowerManagerMarker>()?;

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
    let power_watcher_local = async::Channel::from_channel(power_watcher_local)?;
    let mut watcher_ptr = fidl::endpoints2::ClientEnd::<PowerManagerWatcherMarker>::new(power_watcher_remote);
    power.watch(&mut watcher_ptr)?;

    // Publish service and register service delegate.
    let (service_local, service_remote) = zx::Channel::create()?;
    let service_local = async::Channel::from_channel(service_local)?;
    let mut service_server = fidl::endpoints2::ServerEnd::<gatt::Service_Marker>::new(service_remote);
    let service_proxy = gatt::Service_Proxy::new(service_local);

    let (delegate_local, delegate_remote) = zx::Channel::create()?;
    let delegate_local = async::Channel::from_channel(delegate_local)?;
    let mut delegate_ptr = fidl::endpoints2::ClientEnd::<gatt::ServiceDelegateMarker>::new(delegate_remote);

    let publish = server
        .publish_service(&mut service_info, &mut delegate_ptr, &mut service_server)
        .map_err(|e| Error::from(e.context("Publishing service error")))
        .and_then(|status| match status.error {
            None => Ok(()),
            Some(e) => Err(Error::from(BluetoothError(*e))),
        });

    // This stores the current battery level and a list of peer device IDs that
    // have subscribed to battery level notifications.
    let state = Arc::new(Mutex::new(BatteryState {
        level: 0 as u8,
        service: service_proxy,
        configs: HashSet::new(),
    }));

    // Spin up the power watcher to start handling events.
    let power_watcher_server = power_manager_watcher(state.clone(), power_watcher_local);

    // Set up the GATT service delegate.
    let service_delegate_server = gatt_service_delegate(state.clone(), delegate_local);

    // Listen for incoming connections if the user requested it. Otherwise, this
    // will simply publish the GATT service without advertising.
    if listen {
        let peripheral = connect_to_service::<le::PeripheralMarker>()?;
        let peripheral_state = Arc::new(Mutex::new(BatteryPeripheralState {
            peripheral: peripheral,
            delegate_handle: CancelHandle::new(),
        }));
        start_advertising(peripheral_state)?;
    }

    let main_fut = publish.and_then(|()| power_watcher_server.join(service_delegate_server));

    exec.run(main_fut, /* threads */ 2)
        .map(|((), ())| ())
}
