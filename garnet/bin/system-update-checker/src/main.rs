// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

mod apply;
mod check;
mod errors;
mod manager_manager;
mod manager_service;

use crate::manager_service::RealManagerService;
use failure::{Error, ResultExt};
use fidl_fuchsia_update::ManagerRequestStream;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::fx_log_err;
use futures::prelude::*;

const MAX_CONCURRENT_CONNECTIONS: usize = 100;
const SERVER_THREADS: usize = 1;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["system-update-checker"]).context("syslog init failed")?;
    let mut executor = fasync::Executor::new().context("executor creation failed")?;

    // TODO(PKG-768) use _manager_manager
    let (_manager_manager, manager_service) = RealManagerService::new_manager_and_service();

    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(move |stream| IncomingServices::Manager(stream, manager_service.clone()));
    fs.take_and_serve_directory_handle().context("ServiceFs::take_and_serve_directory_handle")?;
    let fidl_fut = fs.for_each_concurrent(MAX_CONCURRENT_CONNECTIONS, |incoming_service| {
        handle_incoming_service(incoming_service)
            .unwrap_or_else(|e| fx_log_err!("error handling client connection: {}", e))
    });

    executor.run(fidl_fut, SERVER_THREADS);
    Ok(())
}

enum IncomingServices {
    Manager(ManagerRequestStream, RealManagerService),
}

async fn handle_incoming_service(incoming_service: IncomingServices) -> Result<(), Error> {
    match incoming_service {
        IncomingServices::Manager(request_stream, manager_service) => {
            await!(manager_service.handle_request_stream(request_stream))
        }
    }
}
