// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]
#![allow(clippy::let_unit_value)]

use anyhow::{anyhow, Context as _, Error};
use fuchsia_component::server::ServiceFs;
use futures::{lock::Mutex, prelude::*, stream::FuturesUnordered};
use http_request::FuchsiaHyperHttpRequest;
use omaha_client::{
    cup_ecdsa::StandardCupv2Handler, state_machine::StateMachineBuilder, time::StandardTimeSource,
};
use std::cell::RefCell;
use std::rc::Rc;
use tracing::{error, info};

mod api_metrics;
mod app_set;
mod channel;
mod cobalt;
mod configuration;
mod feedback_annotation;
mod fidl;
mod http_request;
mod inspect;
mod install_plan;
mod installer;
mod metrics;
mod observer;
mod policy;
mod storage;
mod timer;

use configuration::{ChannelSource, ClientConfiguration};

#[fuchsia::main]
pub fn main() -> Result<(), Error> {
    info!("Starting omaha client...");

    let mut executor = fuchsia_async::LocalExecutor::new().context("Error creating executor")?;

    executor.run_singlethreaded(main_inner()).map_err(|err| {
        // Use anyhow to print the error chain.
        let err = anyhow!(err);
        error!("error running omaha-client: {:#}", err);
        err
    })
}

async fn main_inner() -> Result<(), Error> {
    let channel_configs = channel::get_configs().ok();
    info!("Omaha channel config: {:?}", channel_configs);

    let ClientConfiguration { platform_config, app_set, channel_data } =
        ClientConfiguration::initialize(channel_configs.as_ref())
            .await
            .expect("Unable to read necessary client configuration");

    info!("Update config: {:?}", platform_config);

    let futures = FuturesUnordered::new();

    // Cobalt metrics
    let (metrics_reporter, cobalt_fut) = metrics::CobaltMetricsReporter::new();
    futures.push(cobalt_fut.boxed_local());

    let (api_metrics_reporter, cobalt_fut) = api_metrics::CobaltApiMetricsReporter::new();
    futures.push(cobalt_fut.boxed_local());

    let mut fs = ServiceFs::new_local();
    fs.take_and_serve_directory_handle().context("while serving directory handle")?;

    // Inspect
    let inspector = fuchsia_inspect::Inspector::new();
    inspect_runtime::serve(&inspector, &mut fs)?;
    let root = inspector.root();
    let configuration_node = inspect::ConfigurationNode::new(root.create_child("configuration"));
    configuration_node.set(&platform_config);
    let apps_node = inspect::AppsNode::new(root.create_child("apps"));
    apps_node.set(&app_set);
    let state_node = inspect::StateNode::new(root.create_child("state"));
    let schedule_node = inspect::ScheduleNode::new(root.create_child("schedule"));
    let protocol_state_node = inspect::ProtocolStateNode::new(root.create_child("protocol_state"));
    let last_results_node = inspect::LastResultsNode::new(root.create_child("last_results"));
    let platform_metrics_node = root.create_child("platform_metrics");
    root.record_string("channel_source", format!("{:?}", channel_data.source));

    // HTTP
    let http = FuchsiaHyperHttpRequest::new();

    let cup_handler: Option<StandardCupv2Handler> =
        platform_config.omaha_public_keys.as_ref().map(StandardCupv2Handler::new);

    let app_set = Rc::new(Mutex::new(app_set));

    // Installer
    let installer = installer::FuchsiaInstaller::new(Rc::clone(&app_set));

    // Storage
    let stash = storage::Stash::new("omaha-client").await;
    let stash_ref = Rc::new(Mutex::new(stash));

    // Policy
    let mut policy_engine_builder = policy::FuchsiaPolicyEngineBuilder::new_from_args()
        .time_source(StandardTimeSource)
        .metrics_reporter(metrics_reporter.clone());

    if let Some(channel_config) = channel_data.config {
        if let Some(interval_secs) = channel_config.check_interval_secs {
            policy_engine_builder = policy_engine_builder
                .periodic_interval(std::time::Duration::from_secs(interval_secs));
        }
    }

    let policy_engine = policy_engine_builder.build();
    futures.push(policy_engine.start_watching_ui_activity().boxed_local());
    let policy_config = policy_engine.get_config();
    let _policy_config_node =
        inspect::PolicyConfigNode::new(root.create_child("policy_config"), policy_config);

    // StateMachine
    let (state_machine_control, state_machine) = StateMachineBuilder::new(
        policy_engine,
        http,
        installer,
        timer::FuchsiaTimer,
        metrics_reporter,
        Rc::clone(&stash_ref),
        platform_config.clone(),
        Rc::clone(&app_set),
        cup_handler,
    )
    .start()
    .await;

    // Notify Cobalt of the current channel and set the current app id in any feedback reports.
    let notify_cobalt = channel_data.source == ChannelSource::VbMeta;
    if notify_cobalt {
        futures.push(
            cobalt::notify_cobalt_current_software_distribution(Rc::clone(&app_set)).boxed_local(),
        );
    }

    futures.push(feedback_annotation::publish_ids_to_feedback(Rc::clone(&app_set)).boxed_local());

    // Serve FIDL API
    let fidl = fidl::FidlServer::new(
        state_machine_control,
        stash_ref,
        Rc::clone(&app_set),
        apps_node,
        state_node,
        channel_configs,
        Box::new(api_metrics_reporter),
        channel_data.name,
    );
    let fidl = Rc::new(RefCell::new(fidl));

    // Observe state machine events
    let mut observer = observer::FuchsiaObserver::new(
        Rc::clone(&fidl),
        schedule_node,
        protocol_state_node,
        last_results_node,
        app_set,
        notify_cobalt,
        platform_metrics_node,
    );

    futures.push(observer.start_handling_crash_reports());
    futures.push(
        async move {
            futures::pin_mut!(state_machine);

            while let Some(event) = state_machine.next().await {
                observer.on_event(event).await;
            }
        }
        .boxed_local(),
    );
    futures.push(fidl::FidlServer::run(fidl, fs).boxed_local());

    futures.collect::<()>().await;
    Ok(())
}
