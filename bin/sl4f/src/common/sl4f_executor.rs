// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use failure::ResultExt;
use futures::channel::mpsc;
use futures::{FutureExt, StreamExt};
use parking_lot::RwLock;
use std::sync::Arc;

use common::bluetooth_commands::method_to_fidl;
use common::bluetooth_facade::BluetoothFacade;
use common::sl4f_types::{AsyncRequest, AsyncResponse};

pub fn run_fidl_loop(
    bt_facade: Arc<RwLock<BluetoothFacade>>, receiver: mpsc::UnboundedReceiver<AsyncRequest>,
) {
    let mut executor = async::Executor::new()
        .context("Error creating event loop")
        .expect("Failed to create an executor!");

    let receiver_fut = receiver.for_each_concurrent(move |request| match request {
        AsyncRequest {
            tx,
            id,
            name,
            params,
        } => {
            let bt_facade = bt_facade.clone();
            fx_log_info!(tag: "sl4f_asyc_execute",
                "Received sync request: {:?}, {:?}, {:?}, {:?}",
                tx, id, name, params
            );

            let fidl_fut = method_to_fidl(name.clone(), params.clone(), bt_facade.clone());
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
