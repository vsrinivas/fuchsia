// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fuchsia_async::{self as fasync, unsafe_many_futures};
use fuchsia_bluetooth::error::Error as BTError;
use fuchsia_syslog::macros::*;
use futures::channel::mpsc;
use futures::future::ready as fready;
use futures::prelude::*;
use futures::StreamExt;
use parking_lot::RwLock;
use serde_json::Value;
use std::sync::Arc;

// Sl4f related inclusions
use crate::server::sl4f::Sl4f;
use crate::server::sl4f_types::{AsyncRequest, AsyncResponse, FacadeType};

// Translation layers go here (i.e netstack_method_to_fidl)
use crate::bluetooth::commands::ble_advertise_method_to_fidl;
use crate::bluetooth::commands::ble_method_to_fidl;
use crate::bluetooth::commands::gatt_client_method_to_fidl;
use crate::wlan::commands::wlan_method_to_fidl;

pub fn run_fidl_loop(
    executor: &mut fasync::Executor,
    sl4f_session: Arc<RwLock<Sl4f>>, receiver: mpsc::UnboundedReceiver<AsyncRequest>,
) {
    let receiver_fut = receiver.map(|request| match request {
        AsyncRequest {
            tx,
            id,
            method_type,
            name,
            params,
        } => {
            let curr_sl4f_session = sl4f_session.clone();
            fx_log_info!(tag: "run_fidl_loop",
                "Received synchronous request: {:?}, {:?}, {:?}, {:?}, {:?}",
                tx, id, method_type, name, params
            );

            let fidl_fut = method_to_fidl(
                method_type.clone(),
                name.clone(),
                params.clone(),
                curr_sl4f_session.clone(),
            );
            fidl_fut.and_then(move |resp| {
                let response = AsyncResponse::new(Ok(resp));

                // Ignore any tx sending errors, other requests can still be outstanding
                tx.send(response).unwrap();
                future::ready(Ok(()))
            }).map(|r| {
                if let Err(e) = r {
                    println!("ERROR in run_fidl_loop response processing: {}", e);
                }
            })
        }
    }).buffer_unordered(10) // TODO figure out a good parallel value for this
    .collect::<()>();

    executor.run_singlethreaded(receiver_fut);
}

fn method_to_fidl(
    method_type: String, method_name: String, args: Value, sl4f_session: Arc<RwLock<Sl4f>>,
) -> impl Future<Output = Result<Value, Error>> {
    unsafe_many_futures!(MethodType, [BleAdvertiseFacade, Bluetooth, GattClientFacade, Wlan, Error]);
    match FacadeType::from_str(&method_type) {
        FacadeType::BleAdvertiseFacade => MethodType::BleAdvertiseFacade(ble_advertise_method_to_fidl(
                method_name,
                args,
                sl4f_session.write().get_ble_advertise_facade(),
        )),
        FacadeType::Bluetooth => MethodType::Bluetooth(ble_method_to_fidl(
            method_name,
            args,
            sl4f_session.write().get_bt_facade(),
        )),
        FacadeType::GattClientFacade => MethodType::GattClientFacade(gatt_client_method_to_fidl(
                method_name,
                args,
                sl4f_session.write().get_gatt_client_facade(),
        )),
        FacadeType::Wlan => MethodType::Wlan(wlan_method_to_fidl(
            method_name,
            args,
            sl4f_session.write().get_wlan_facade()
        )),
        _ => MethodType::Error(fready(Err(BTError::new("Invalid FIDL method type").into()))),
    }
}
