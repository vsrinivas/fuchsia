// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Dev authenticator is a test-only authenticator that produces fake authentication
//! events to be consumed by identity components during tests.

mod storage_unlock;

use crate::storage_unlock::StorageUnlockMechanism;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use log::{error, info};

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Failed to initialize logger");
    info!("Starting dev authenticator");

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn(async move {
            StorageUnlockMechanism::handle_requests_from_stream(stream)
                .await
                .unwrap_or_else(|e| error!("Error handling storage unlock stream: {:?}", e));
        });
    });

    fs.take_and_serve_directory_handle().expect("Failed to serve directory");

    fs.collect::<()>().await;
}
