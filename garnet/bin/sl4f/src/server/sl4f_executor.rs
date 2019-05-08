// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fuchsia_bluetooth::error::Error as BTError;
use fuchsia_syslog::macros::*;
use futures::channel::mpsc;
use futures::StreamExt;
use parking_lot::RwLock;
use serde_json::Value;
use std::sync::Arc;

// Sl4f related inclusions
use crate::server::sl4f::Sl4f;
use crate::server::sl4f_types::{AsyncRequest, AsyncResponse, FacadeType};

// Translation layers go here (i.e netstack_method_to_fidl)
use crate::audio::commands::audio_method_to_fidl;
use crate::auth::commands::auth_method_to_fidl;
use crate::bluetooth::commands::ble_advertise_method_to_fidl;
use crate::bluetooth::commands::ble_method_to_fidl;
use crate::bluetooth::commands::bt_control_method_to_fidl;
use crate::bluetooth::commands::gatt_client_method_to_fidl;
use crate::bluetooth::commands::gatt_server_method_to_fidl;
use crate::netstack::commands::netstack_method_to_fidl;
use crate::scenic::commands::scenic_method_to_fidl;
use crate::setui::commands::setui_method_to_fidl;
use crate::wlan::commands::wlan_method_to_fidl;

pub async fn run_fidl_loop(
    sl4f_session: Arc<RwLock<Sl4f>>,
    receiver: mpsc::UnboundedReceiver<AsyncRequest>,
) {
    const CONCURRENT_REQ_LIMIT: usize = 10; // TODO(CONN-6) figure out a good parallel value for this

    let session = &sl4f_session;
    let handler = async move |request| {
        await!(handle_request(Arc::clone(session), request)).unwrap();
    };

    let receiver_fut = receiver.for_each_concurrent(CONCURRENT_REQ_LIMIT, handler);

    await!(receiver_fut);
}

async fn handle_request(
    sl4f_session: Arc<RwLock<Sl4f>>,
    request: AsyncRequest,
) -> Result<(), Error> {
    match request {
        AsyncRequest { tx, id, method_type, name, params } => {
            let curr_sl4f_session = sl4f_session.clone();
            fx_log_info!(tag: "run_fidl_loop",
                         "Received synchronous request: {:?}, {:?}, {:?}, {:?}, {:?}",
                         tx, id, method_type, name, params);
            match await!(method_to_fidl(method_type, name, params, curr_sl4f_session)) {
                Ok(response) => {
                    let async_response = AsyncResponse::new(Ok(response));

                    // Ignore any tx sending errors since there is not a recovery path.  The
                    // connection to the test server may be broken.
                    let _ = tx.send(async_response);
                }
                Err(e) => {
                    println!("Error returned from calling method_to_fidl {}", e);
                    let async_response = AsyncResponse::new(Err(e));

                    // Ignore any tx sending errors since there is not a recovery path.  The
                    // connection to the test server may be broken.
                    let _ = tx.send(async_response);
                }
            };
        }
    }
    Ok(())
}

async fn method_to_fidl(
    method_type: String,
    method_name: String,
    args: Value,
    sl4f_session: Arc<RwLock<Sl4f>>,
) -> Result<Value, Error> {
    match FacadeType::from_str(&method_type) {
        FacadeType::AudioFacade => {
            await!(audio_method_to_fidl(method_name, args, sl4f_session.read().get_audio_facade(),))
        }
        FacadeType::AuthFacade => {
            await!(auth_method_to_fidl(method_name, args, sl4f_session.read().get_auth_facade(),))
        }
        FacadeType::BleAdvertiseFacade => await!(ble_advertise_method_to_fidl(
            method_name,
            args,
            sl4f_session.read().get_ble_advertise_facade(),
        )),
        FacadeType::Bluetooth => {
            await!(ble_method_to_fidl(method_name, args, sl4f_session.read().get_bt_facade(),))
        }
        FacadeType::BluetoothControlFacade => await!(bt_control_method_to_fidl(
            method_name,
            args,
            sl4f_session.read().get_bt_control_facade(),
        )),
        FacadeType::GattClientFacade => await!(gatt_client_method_to_fidl(
            method_name,
            args,
            sl4f_session.read().get_gatt_client_facade(),
        )),
        FacadeType::GattServerFacade => await!(gatt_server_method_to_fidl(
            method_name,
            args,
            sl4f_session.read().get_gatt_server_facade(),
        )),
        FacadeType::NetstackFacade => await!(netstack_method_to_fidl(
            method_name,
            args,
            sl4f_session.read().get_netstack_facade(),
        )),
        FacadeType::ScenicFacade => await!(scenic_method_to_fidl(
            method_name,
            args,
            sl4f_session.read().get_scenic_facade(),
        )),
        FacadeType::SetUiFacade => await!(setui_method_to_fidl(
            method_name,
            args,
            sl4f_session.read().get_setui_facade(),
        )),
        FacadeType::Wlan => {
            await!(wlan_method_to_fidl(method_name, args, sl4f_session.read().get_wlan_facade()))
        }
        _ => Err(BTError::new("Invalid FIDL method type").into()),
    }
}
