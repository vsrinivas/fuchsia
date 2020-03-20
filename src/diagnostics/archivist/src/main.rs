// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Archivist collects and stores diagnostic data from components.

#![warn(missing_docs)]

use {
    anyhow::{Context, Error},
    archivist_lib::{
        archive, archive_accessor, component_events, configs, data_stats, diagnostics, inspect,
        logs,
    },
    argh::FromArgs,
    fidl_fuchsia_sys_internal::ComponentEventProviderMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{component, health::Reporter},
    futures::{future, stream, FutureExt, StreamExt},
    io_util,
    parking_lot::RwLock,
    std::{
        path::{Path, PathBuf},
        sync::Arc,
    },
};

/// Monitor, collect, and store diagnostics from components.
#[derive(Debug, Default, FromArgs)]
pub struct Args {
    /// disables proxying kernel logger
    #[argh(switch)]
    disable_klog: bool,

    /// path to a JSON configuration file
    #[argh(option)]
    config_path: PathBuf,
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new()?;

    let provider = connect_to_service::<ComponentEventProviderMarker>()
        .context("failed to connect to entity resolver")?;

    diagnostics::init();

    let events_stream_fut =
        component_events::listen(provider, diagnostics::root().create_child("event_stats"));

    let opt: Args = argh::from_env();
    let log_manager = logs::LogManager::new(diagnostics::root().create_child("log_stats"));

    let archivist_configuration: configs::Config = match configs::parse_config(&opt.config_path) {
        Ok(config) => config,
        Err(parsing_error) => panic!("Parsing configuration failed: {}", parsing_error),
    };

    let num_threads = archivist_configuration.num_threads;
    if !opt.disable_klog {
        let wait_for_initial_drain = log_manager.spawn_klog_drainer()?;
        executor.run(wait_for_initial_drain, num_threads)?;
    }

    let mut fs = ServiceFs::new();
    diagnostics::serve(&mut fs)?;

    let writer = if let Some(archive_path) = &archivist_configuration.archive_path {
        let writer = archive::ArchiveWriter::open(archive_path)?;
        fs.add_remote(
            "archive",
            io_util::open_directory_in_namespace(
                &archive_path.to_string_lossy(),
                io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
            )?,
        );
        Some(writer)
    } else {
        None
    };

    // The Inspect Repository offered to the ALL_ACCESS pipeline. This repository is unique
    // in that it has no statically configured selectors, meaning all diagnostics data is visible.
    // This should not be used for production services.
    let all_inspect_repository = Arc::new(RwLock::new(inspect::InspectDataRepository::new(None)));

    // Set up loading feedback pipeline configs.
    let pipelines_node = diagnostics::root().create_child("pipelines");
    let feedback_pipeline = pipelines_node.create_child("feedback");

    let feedback_config = configs::PipelineConfig::from_directory("/config/data/feedback");
    feedback_config.record_to_inspect(&feedback_pipeline);

    // Do not set the state to error if the feedback pipeline simply doesn't exist.
    let has_pipeline_error =
        Path::new("/config/data/feedback").is_dir() && feedback_config.has_error();

    if let Some(to_summarize) = &archivist_configuration.summarized_dirs {
        data_stats::add_stats_nodes(component::inspector().root(), to_summarize.clone())?;
    }

    let archivist_state = archive::ArchivistState::new(
        archivist_configuration,
        all_inspect_repository.clone(),
        writer,
    )?;

    let log_manager2 = log_manager.clone();
    let log_manager3 = log_manager.clone();

    fs.dir("svc")
        .add_fidl_service(move |stream| {
            log_manager2.spawn_log_handler(stream);
        })
        .add_fidl_service(move |stream| {
            log_manager3.spawn_log_sink_handler(stream);
        })
        .add_fidl_service(move |stream| {
            let all_archive_accessor =
                archive_accessor::ArchiveAccessor::new(all_inspect_repository.clone());
            all_archive_accessor.spawn_archive_accessor_server(stream)
        });

    fs.take_and_serve_directory_handle()?;

    let running_archivist = events_stream_fut.then(move |events_result| {
        let events = match events_result {
            Ok(events) => {
                if has_pipeline_error {
                    component::health().set_unhealthy("Pipeline config has an error");
                } else {
                    component::health().set_ok();
                }
                events
            }
            Err(e) => {
                component::health().set_unhealthy(&format!(
                    "Failed to listen for component lifecycle events: {:?}",
                    e
                ));
                stream::empty().boxed()
            }
        };
        archive::run_archivist(archivist_state, events)
    });
    let running_service_fs = fs.collect::<()>().map(Ok);
    let both = future::try_join(running_service_fs, running_archivist);
    executor.run(both, num_threads)?;
    Ok(())
}
