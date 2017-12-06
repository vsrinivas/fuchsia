// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate fidl;
extern crate fuchsia_app;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate garnet_public_lib_bluetooth_fidl;
extern crate garnet_public_lib_power_fidl;
extern crate tokio_core;

use bt::gatt;
use fidl::{ClientEnd, FidlService, InterfacePtr};
use fuchsia_app::client::ApplicationContext;
use futures::{future, Future};
use garnet_public_lib_bluetooth_fidl as bt;
use garnet_public_lib_power_fidl::{BatteryStatus, PowerManager, PowerManagerWatcher};
use std::cell::RefCell;
use std::collections::HashSet;
use std::error::Error;
use std::rc::Rc;
use tokio_core::reactor;

const BATTERY_LEVEL_ID: u64 = 0;
const BATTERY_SERVICE_UUID: &'static str = "0000180f-0000-1000-8000-00805f9b34fb";
const BATTERY_LEVEL_UUID: &'static str = "00002A19-0000-1000-8000-00805f9b34fb";

// A custom error type to capture both FIDL binding and GATT API errors.
#[derive(Debug)]
struct ErrorResult {
    message: String,
}

impl ErrorResult {
    pub fn new(msg: String) -> ErrorResult {
        ErrorResult { message: msg }
    }
}

impl From<fidl::Error> for ErrorResult {
    fn from(error: fidl::Error) -> ErrorResult {
        ErrorResult::new(format!("{:?}", error))
    }
}

impl From<bt::Error> for ErrorResult {
    fn from(error: bt::Error) -> ErrorResult {
        match error.description {
            None => ErrorResult::new("Unknown error".to_string()),
            Some(msg) => ErrorResult::new(msg),
        }
    }
}

impl From<std::io::Error> for ErrorResult {
    fn from(error: std::io::Error) -> ErrorResult {
        ErrorResult::new(error.description().to_owned())
    }
}

impl From<zx::Status> for ErrorResult {
    fn from(status: zx::Status) -> ErrorResult {
        ErrorResult::from(std::io::Error::from(status))
    }
}

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

        future::ok(())
    }

    // This is called when a remote device requests to read the current battery
    // level.
    type OnReadValue = fidl::ServerImmediate<(Option<Vec<u8>>, gatt::ErrorCode)>;
    fn on_read_value(&mut self, _id: u64, _offset: i32) -> Self::OnReadValue {
        future::ok((
            Some(vec![self.state.borrow().level]),
            gatt::ErrorCode::NoError,
        ))
    }

    // The battery level characteristic that we publish below does not have the
    // "write" property, so the following delegate methods will never be called:

    type OnWriteValue = fidl::ServerImmediate<gatt::ErrorCode>;
    fn on_write_value(&mut self, _id: u64, _offset: u16, _value: Vec<u8>) -> Self::OnWriteValue {
        future::ok(gatt::ErrorCode::NotPermitted)
    }

    type OnWriteWithoutResponse = fidl::ServerImmediate<()>;
    fn on_write_without_response(
        &mut self,
        _id: u64,
        _offset: u16,
        _value: Vec<u8>,
    ) -> Self::OnWriteWithoutResponse {
        future::ok(())
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
        future::ok(())
    }
}

fn main() {
    if let Err(e) = main_res() {
        println!("Error: {:?}", e);
    }
}

fn main_res() -> Result<(), ErrorResult> {
    let mut core = reactor::Core::new()?;
    let handle = core.handle();

    let app_context = ApplicationContext::new(&handle)?;
    let server = app_context.connect_to_service::<gatt::Server_::Service>(
        &handle,
    )?;
    let power = app_context.connect_to_service::<PowerManager::Service>(
        &handle,
    )?;

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
        .map_err(ErrorResult::from)
        .and_then(|status| match status.error {
            None => Ok(()),
            Some(e) => Err(ErrorResult::from(*e)),
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
    )?.map_err(|e| {
        eprintln!("PowerManagerWatcher server error: {:?}", e);
        ()
    });

    handle.spawn(power_watcher_server);

    // Set up the GATT service delegate.
    let start_server = fidl::Server::new(
        bt::ServiceDelegate::Dispatcher(BatteryService::new(state.clone())),
        delegate_local,
        &handle,
    )?.map_err(|e| ErrorResult::from(e));

    core.run(publish.and_then(|_| start_server))
}
