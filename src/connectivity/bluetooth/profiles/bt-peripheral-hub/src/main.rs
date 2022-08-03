// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_component::server::ServiceFs;
use std::sync::Arc;

mod error;
mod fidl_service;
mod peripheral_state;
mod reporter;
mod watcher;

use fidl_service::run_services;
use peripheral_state::PeripheralState;

#[fuchsia::main(logging_tags = ["bt-peripheral-hub"])]
async fn main() -> Result<(), Error> {
    let fs = ServiceFs::new();

    let shared_state = Arc::new(PeripheralState::new());
    let service_handler = run_services(fs, shared_state);

    service_handler.await
}
