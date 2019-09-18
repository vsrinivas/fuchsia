// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod apply;
mod channel;
mod check;
mod config;
mod connect;
mod errors;
mod inspect;
mod last_update_storage;
mod poller;
mod provider_handler;
mod update_manager;
mod update_monitor;
mod update_service;

use crate::apply::Initiator;
use crate::config::Config;
use crate::poller::run_periodic_update_check;
use crate::provider_handler::ProviderHandler;
use crate::update_service::{RealUpdateManager, RealUpdateService};
use failure::{Error, ResultExt};
use fidl_fuchsia_update::ManagerRequestStream;
use fidl_fuchsia_update_channel::ProviderRequestStream;
use forced_fdr::perform_fdr_if_necessary;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect as finspect;
use fuchsia_syslog::{fx_log_err, fx_log_warn};
use futures::prelude::*;
use std::sync::Arc;
use std::time::Duration;

const MAX_CONCURRENT_CONNECTIONS: usize = 100;

#[fasync::run(1)]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["system-update-checker"]).context("syslog init failed")?;

    let config = Config::load_from_config_data_or_default();
    if let Some(url) = config.update_package_url() {
        fx_log_warn!("Ignoring custom update package url: {}", url);
    }

    let inspector = finspect::Inspector::new();

    let mut target_channel_manager =
        channel::TargetChannelManager::new(connect::ServiceConnector, "/misc/ota");
    if let Err(e) = target_channel_manager.update().await {
        fx_log_err!("while updating the target channel: {}", e);
    }

    let (current_channel_manager, current_channel_notifier) =
        channel::build_current_channel_manager_and_notifier(
            connect::ServiceConnector,
            "/misc/ota",
        )?;
    let channel_fut = current_channel_notifier.run();

    let update_manager = Arc::new(
        RealUpdateManager::new(
            target_channel_manager,
            current_channel_manager,
            inspector.root().create_child("update-manager"),
        )
        .await,
    );
    let info_handler = ProviderHandler::default();

    let mut fs = ServiceFs::new();
    let update_manager_clone = update_manager.clone();
    fs.dir("svc")
        .add_fidl_service(move |stream| {
            IncomingServices::Manager(stream, RealUpdateService::new(update_manager_clone.clone()))
        })
        .add_fidl_service(move |stream| IncomingServices::Provider(stream, info_handler.clone()));

    inspector.export(&mut fs);

    fs.take_and_serve_directory_handle().context("ServiceFs::take_and_serve_directory_handle")?;
    let fidl_fut = fs.for_each_concurrent(MAX_CONCURRENT_CONNECTIONS, |incoming_service| {
        handle_incoming_service(incoming_service)
            .unwrap_or_else(|e| fx_log_err!("error handling client connection: {}", e))
    });

    let cron_fut = run_periodic_update_check(update_manager.clone(), &config);

    let first_update_fut = async move {
        if config.poll_frequency().is_some() {
            fasync::Timer::new(fasync::Time::after(Duration::from_secs(60).into())).await;
            update_manager.try_start_update(Initiator::Automatic, None);
        }
    };

    future::join5(channel_fut, fidl_fut, cron_fut, first_update_fut, perform_fdr_if_necessary())
        .await;

    Ok(())
}

enum IncomingServices {
    Manager(ManagerRequestStream, RealUpdateService),
    Provider(ProviderRequestStream, ProviderHandler),
}

async fn handle_incoming_service(incoming_service: IncomingServices) -> Result<(), Error> {
    match incoming_service {
        IncomingServices::Manager(request_stream, update_service) => {
            update_service.handle_request_stream(request_stream).await
        }
        IncomingServices::Provider(request_stream, handler) => {
            handler.handle_request_stream(request_stream).await
        }
    }
}
