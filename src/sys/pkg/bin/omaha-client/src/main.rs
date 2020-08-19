// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fuchsia_component::server::ServiceFs;
use futures::{lock::Mutex, prelude::*, stream::FuturesUnordered};
use http_request::FuchsiaHyperHttpRequest;
use log::{error, info};
use omaha_client::{state_machine::StateMachineBuilder, time::StandardTimeSource};
use std::cell::RefCell;
use std::rc::Rc;

mod api_metrics;
mod channel;
mod cobalt;
mod configuration;
mod fidl;
mod http_request;
mod inspect;
mod install_plan;
mod installer;
mod metrics;
mod observer;
mod policy;
mod storage;
mod temp_installer;
mod timer;

use configuration::{ChannelSource, ClientConfiguration};

fn main() -> Result<(), Error> {
    fuchsia_syslog::init().expect("Can't init logger");
    info!("Starting omaha client...");

    let mut executor = fuchsia_async::Executor::new().context("Error creating executor")?;

    executor.run_singlethreaded(async {
        let channel_configs = channel::get_configs().ok();
        info!("Omaha channel config: {:?}", channel_configs);

        let ClientConfiguration { platform_config, app_set, channel_data } =
            ClientConfiguration::initialize(channel_configs.as_ref())
                .await
                .expect("Unable to read necessary client configuration");

        info!("Omaha app set: {:?}", app_set.to_vec().await);
        info!("Update config: {:?}", platform_config);

        let futures = FuturesUnordered::new();

        // Cobalt metrics
        let (metrics_reporter, cobalt_fut) = metrics::CobaltMetricsReporter::new();
        futures.push(cobalt_fut.boxed_local());

        let (api_metrics_reporter, cobalt_fut) = api_metrics::CobaltApiMetricsReporter::new();
        futures.push(cobalt_fut.boxed_local());

        let mut fs = ServiceFs::new_local();
        fs.take_and_serve_directory_handle()?;

        // Inspect
        let inspector = fuchsia_inspect::Inspector::new();
        inspector.serve(&mut fs)?;
        let root = inspector.root();
        let configuration_node =
            inspect::ConfigurationNode::new(root.create_child("configuration"));
        configuration_node.set(&platform_config);
        let apps_node = inspect::AppsNode::new(root.create_child("apps"));
        apps_node.set(&app_set.to_vec().await);
        let state_node = inspect::StateNode::new(root.create_child("state"));
        let schedule_node = inspect::ScheduleNode::new(root.create_child("schedule"));
        let protocol_state_node =
            inspect::ProtocolStateNode::new(root.create_child("protocol_state"));
        let last_results_node = inspect::LastResultsNode::new(root.create_child("last_results"));
        let platform_metrics_node = root.create_child("platform_metrics");
        root.record_string("channel_source", format!("{:?}", channel_data.source));

        // HTTP
        let http = FuchsiaHyperHttpRequest::new();

        // Installer
        let installer = temp_installer::FuchsiaInstaller::new()?;

        // Storage
        let stash = storage::Stash::new("omaha-client").await;
        let stash_ref = Rc::new(Mutex::new(stash));

        // Policy
        let mut policy_engine_builder = policy::FuchsiaPolicyEngineBuilder
            .time_source(StandardTimeSource)
            .load_config_from("/config/data");

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
            stash_ref.clone(),
            platform_config.clone(),
            app_set.clone(),
        )
        .start()
        .await;

        // Notify Cobalt current channel
        let notify_cobalt = channel_data.source == ChannelSource::VbMeta;
        if notify_cobalt {
            futures.push(
                cobalt::notify_cobalt_current_software_distribution(app_set.clone()).boxed_local(),
            );
        }

        // Serve FIDL API
        let fidl = fidl::FidlServer::new(
            state_machine_control,
            stash_ref,
            app_set.clone(),
            apps_node,
            state_node,
            channel_configs,
            Box::new(api_metrics_reporter),
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

        futures.push(check_and_set_system_health().boxed_local());

        futures.collect::<()>().await;
        Ok(())
    })
}

async fn check_and_set_system_health() {
    if let Err(err) = system_health_check::check_system_health().await {
        error!("error during system health check: {}", err);
        return;
    }
    info!("Marking current slot as good...");
    system_health_check::set_active_configuration_healthy().await;
}
