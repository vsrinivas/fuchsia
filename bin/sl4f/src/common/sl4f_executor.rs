// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use failure::ResultExt;
use futures::channel::mpsc;
use futures::{FutureExt, StreamExt};
use parking_lot::RwLock;
use std::sync::Arc;

use common::sl4f::method_to_fidl;
use common::sl4f::Sl4f;
use common::sl4f_types::{AsyncRequest, AsyncResponse};

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
