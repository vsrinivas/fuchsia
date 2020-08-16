// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod apply;
mod channel;
mod channel_handler;
mod check;
mod config;
mod connect;
mod errors;
mod last_update_storage;
mod poller;
mod rate_limiter;
mod update_manager;
mod update_monitor;
mod update_service;

use {
    crate::{
        channel_handler::ChannelHandler,
        config::Config,
        poller::run_periodic_update_check,
        update_service::{RealUpdateManager, UpdateService},
    },
    anyhow::{anyhow, Context as _, Error},
    fidl_fuchsia_update_channel::ProviderRequestStream,
    fidl_fuchsia_update_channelcontrol::ChannelControlRequestStream,
    fidl_fuchsia_update_ext::{CheckOptions, Initiator},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as finspect,
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    futures::{prelude::*, stream::FuturesUnordered},
    std::{sync::Arc, time::Duration},
};

const MAX_CONCURRENT_CONNECTIONS: usize = 100;

#[fasync::run(1)]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["system-update-checker"]).context("syslog init failed")?;

    let config = Config::load_from_config_data_or_default();
    if let Some(url) = config.update_package_url() {
        fx_log_warn!("Ignoring custom update package url: {}", url);
    }

    let inspector = finspect::Inspector::new();

    let target_channel_manager =
        channel::TargetChannelManager::new(connect::ServiceConnector, "/misc/ota");
    if let Err(e) = target_channel_manager.update().await {
        fx_log_err!("while updating the target channel: {:#}", anyhow!(e));
    }
    let target_channel_manager = Arc::new(target_channel_manager);

    let futures = FuturesUnordered::new();

    let (current_channel_manager, current_channel_notifier) =
        channel::build_current_channel_manager_and_notifier(
            connect::ServiceConnector,
            "/misc/ota",
        )?;
    futures.push(current_channel_notifier.run().boxed());
    let current_channel_manager = Arc::new(current_channel_manager);

    let (mut update_manager, update_manager_fut) = RealUpdateManager::new(
        Arc::clone(&target_channel_manager),
        Arc::clone(&current_channel_manager),
        inspector.root().create_child("update-manager"),
    )
    .await
    .start();
    futures.push(update_manager_fut.boxed());

    let mut fs = ServiceFs::new();
    let update_manager_clone = update_manager.clone();
    let channel_handler =
        Arc::new(ChannelHandler::new(current_channel_manager, target_channel_manager));
    let channel_handler_clone = Arc::clone(&channel_handler);
    let channel_handler_provider_clone = Arc::clone(&channel_handler);

    fs.dir("svc")
        .add_fidl_service(move |stream| {
            IncomingServices::Manager(stream, UpdateService::new(update_manager_clone.clone()))
        })
        .add_fidl_service(move |stream| {
            IncomingServices::Provider(stream, Arc::clone(&channel_handler_provider_clone))
        })
        .add_fidl_service(move |stream| {
            IncomingServices::ChannelControl(stream, Arc::clone(&channel_handler_clone))
        });

    inspector.serve(&mut fs)?;

    fs.take_and_serve_directory_handle().context("ServiceFs::take_and_serve_directory_handle")?;
    futures.push(
        fs.for_each_concurrent(MAX_CONCURRENT_CONNECTIONS, |incoming_service| {
            handle_incoming_service(incoming_service).unwrap_or_else(|e| {
                fx_log_err!("error handling client connection: {:#}", anyhow!(e))
            })
        })
        .boxed(),
    );

    futures.push(run_periodic_update_check(update_manager.clone(), &config).boxed());

    futures.push(
        async move {
            if config.poll_frequency().is_some() {
                fasync::Timer::new(fasync::Time::after(Duration::from_secs(60).into())).await;
                let options = CheckOptions::builder().initiator(Initiator::Service).build();
                if let Err(e) = update_manager.try_start_update(options, None).await {
                    fx_log_warn!("Update check failed with error: {:?}", e);
                }
            }
        }
        .boxed(),
    );

    futures.push(check_and_set_system_health().boxed());

    futures.collect::<()>().await;

    Ok(())
}

enum IncomingServices {
    Manager(fidl_fuchsia_update::ManagerRequestStream, UpdateService),
    Provider(ProviderRequestStream, Arc<ChannelHandler>),
    ChannelControl(ChannelControlRequestStream, Arc<ChannelHandler>),
}

async fn handle_incoming_service(incoming_service: IncomingServices) -> Result<(), Error> {
    match incoming_service {
        IncomingServices::Manager(request_stream, mut update_service) => {
            update_service.handle_request_stream(request_stream).await
        }
        IncomingServices::Provider(request_stream, handler) => {
            handler.handle_provider_request_stream(request_stream).await
        }
        IncomingServices::ChannelControl(request_stream, handler) => {
            handler.handle_control_request_stream(request_stream).await
        }
    }
}

async fn check_and_set_system_health() {
    if let Err(err) = check_and_set_system_health_impl().await {
        fx_log_err!("error during system health check: {:#}", anyhow!(err));
    }
}

async fn check_and_set_system_health_impl() -> Result<(), Error> {
    system_health_check::check_system_health().await?;
    Ok(system_health_check::set_active_configuration_healthy().await)
}
