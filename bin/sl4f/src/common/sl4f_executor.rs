// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use bt::error::Error as BTError;
use failure::Error;
use failure::ResultExt;
use futures::channel::mpsc;
use futures::future::ok as fok;
use futures::prelude::*;
use futures::{FutureExt, StreamExt};
use parking_lot::RwLock;
use serde_json::Value;
use std::sync::Arc;

use common::bluetooth_commands::ble_method_to_fidl;

use common::sl4f::Sl4f;
use common::sl4f_types::{AsyncRequest, AsyncResponse, FacadeType};

pub fn run_fidl_loop(
    sl4f_session: Arc<RwLock<Sl4f>>, receiver: mpsc::UnboundedReceiver<AsyncRequest>,
) {
    let mut executor = async::Executor::new()
        .context("Error creating event loop")
        .expect("Failed to create an executor!");

    let receiver_fut = receiver.for_each_concurrent(move |request| match request {
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
                let response = AsyncResponse::new(resp);

                // Ignore any tx sending errors, other requests can still be outstanding
                let _ = tx.send(response);
                Ok(())
            })
        }
    });

    executor
        .run_singlethreaded(receiver_fut)
        .expect("Failed to execute requests from Rouille.");
}

fn method_to_fidl(
    method_type: String, method_name: String, args: Value, sl4f_session: Arc<RwLock<Sl4f>>,
) -> impl Future<Item = Result<Value, Error>, Error = Never> {
    many_futures!(MethodType, [Bluetooth, Wlan, Error]);
    match FacadeType::from_str(method_type) {
        FacadeType::Bluetooth => MethodType::Bluetooth(ble_method_to_fidl(
            method_name,
            args,
            sl4f_session.write().get_bt_facade().clone(),
        )),
        FacadeType::Wlan => MethodType::Wlan(fok(Err(BTError::new(
            "Nice try. WLAN not implemented yet",
        ).into()))),
        _ => MethodType::Error(fok(Err(BTError::new("Invalid FIDL method type").into()))),
    }
}
