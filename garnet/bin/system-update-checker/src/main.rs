// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

mod apply;
mod check;
mod config;
mod errors;
mod info_handler;
mod manager_manager;
mod manager_service;
mod poller;

use crate::config::Config;
use crate::info_handler::InfoHandler;
use crate::manager_service::RealManagerService;
use crate::poller::run_periodic_update_check;
use failure::{Error, ResultExt};
use fidl_fuchsia_update::{InfoRequestStream, ManagerRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{fx_log_err, fx_log_warn};
use futures::prelude::*;

const MAX_CONCURRENT_CONNECTIONS: usize = 100;
const SERVER_THREADS: usize = 1;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["system-update-checker"]).context("syslog init failed")?;
    let mut executor = fasync::Executor::new().context("executor creation failed")?;

    let config = Config::load_from_config_data_or_default();
    if let Some(url) = config.update_package_url() {
        fx_log_warn!("Ignoring custom update package url: {}", url);
    }

    let (manager_manager, manager_service) = RealManagerService::new_manager_and_service();
    let info_handler = InfoHandler::default();

    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(move |stream| IncomingServices::Manager(stream, manager_service.clone()))
        .add_fidl_service(move |stream| IncomingServices::Info(stream, info_handler.clone()));
    fs.take_and_serve_directory_handle().context("ServiceFs::take_and_serve_directory_handle")?;
    let fidl_fut = fs.for_each_concurrent(MAX_CONCURRENT_CONNECTIONS, |incoming_service| {
        handle_incoming_service(incoming_service)
            .unwrap_or_else(|e| fx_log_err!("error handling client connection: {}", e))
    });

    let cron_fut = run_periodic_update_check(manager_manager.clone(), &config);

    executor.run(future::join(fidl_fut, cron_fut), SERVER_THREADS);
    Ok(())
}

enum IncomingServices {
    Manager(ManagerRequestStream, RealManagerService),
    Info(InfoRequestStream, InfoHandler),
}

async fn handle_incoming_service(incoming_service: IncomingServices) -> Result<(), Error> {
    match incoming_service {
        IncomingServices::Manager(request_stream, manager_service) => {
            await!(manager_service.handle_request_stream(request_stream))
        }
        IncomingServices::Info(request_stream, handler) => {
            await!(handler.handle_request_stream(request_stream))
        }
    }
}
