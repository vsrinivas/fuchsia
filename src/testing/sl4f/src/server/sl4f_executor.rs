// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_syslog::macros::*;
use futures::channel::mpsc;
use futures::StreamExt;
use serde_json::Value;
use std::sync::Arc;

// Sl4f related inclusions
use crate::common_utils::error::Sl4fError;
use crate::server::constants::CONCURRENT_REQ_LIMIT;
use crate::server::sl4f::Sl4f;
use crate::server::sl4f_types::{AsyncCommandRequest, AsyncRequest, AsyncResponse, MethodId};

pub async fn run_fidl_loop(sl4f: Arc<Sl4f>, receiver: mpsc::UnboundedReceiver<AsyncRequest>) {
    let handler = move |request| handle_request(Arc::clone(&sl4f), request);

    receiver.for_each_concurrent(CONCURRENT_REQ_LIMIT, handler).await;
}

async fn handle_request(sl4f: Arc<Sl4f>, request: AsyncRequest) {
    match request {
        AsyncRequest::Cleanup(done) => {
            // Cleanup all resources associated with sl4f and notify the blocked request when done.
            sl4f.cleanup().await;
            sl4f.print().await;
            done.send(()).unwrap();
        }
        AsyncRequest::Command(AsyncCommandRequest { tx, method_id, params }) => {
            fx_log_info!(tag: "run_fidl_loop",
                         "Received synchronous request: {:?}, {:?}, {:?}",
                         tx, method_id, params);
            match method_to_fidl(method_id, params, Arc::clone(&sl4f)).await {
                Ok(response) => {
                    let async_response = AsyncResponse::new(Ok(response));

                    // Ignore any tx sending errors since there is not a recovery path.  The
                    // connection to the test server may be broken.
                    let _ = tx.send(async_response);
                }
                Err(e) => {
                    fx_log_err!("Error returned from calling method_to_fidl: {:?}", e);
                    let async_response = AsyncResponse::new(Err(e));

                    // Ignore any tx sending errors since there is not a recovery path.  The
                    // connection to the test server may be broken.
                    let _ = tx.send(async_response);
                }
            };
        }
    }
}

async fn method_to_fidl(method_id: MethodId, args: Value, sl4f: Arc<Sl4f>) -> Result<Value, Error> {
    if let Some(facade) = sl4f.get_facade(&method_id.facade) {
        facade.handle_request(method_id.method, args).await
    } else if sl4f.has_proxy_facade(&method_id.facade) {
        sl4f.handle_proxy_request(method_id.facade, method_id.method, args).await
    } else {
        Err(Sl4fError::new(&format!("Invalid FIDL method type {:?}", &method_id.facade)).into())
    }
}
