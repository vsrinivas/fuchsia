// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Archivist collects and stores diagnostic data from components.

#![warn(missing_docs)]

use {
    anyhow::Error,
    archivist_lib::{archive, archive_accessor, configs, data_stats, diagnostics, inspect, logs},
    argh::FromArgs,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{future, FutureExt, StreamExt},
    io_util,
    parking_lot::RwLock,
    std::{path::PathBuf, sync::Arc},
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

    diagnostics::init();

    let opt: Args = argh::from_env();
    let log_manager = logs::LogManager::new();
    if !opt.disable_klog {
        log_manager.spawn_klogger()?;
    }

    let archivist_configuration: configs::Config = match configs::parse_config(&opt.config_path) {
        Ok(config) => config,
        Err(parsing_error) => panic!("Parsing configuration failed: {}", parsing_error),
    };

    let mut fs = ServiceFs::new();
    diagnostics::serve(&mut fs)?;

    if let Some(archive_path) = &archivist_configuration.archive_path {
        fs.add_remote(
            "archive",
            io_util::open_directory_in_namespace(
                &archive_path.to_string_lossy(),
                io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
            )?,
        );
    }

    // The Inspect Repository offered to the ALL_ACCESS pipeline. This repository is unique
    // in that it has no statically configured selectors, meaning all diagnostics data is visible.
    // This should not be used for production services.
    let all_inspect_repository = Arc::new(RwLock::new(inspect::InspectDataRepository::new(None)));

    if let Some(to_summarize) = &archivist_configuration.summarized_dirs {
        data_stats::add_stats_nodes(&mut fs, to_summarize)?;
    }

    let num_threads = archivist_configuration.num_threads;
    let archivist_state =
        archive::ArchivistState::new(archivist_configuration, all_inspect_repository.clone())?;

    let log_manager2 = log_manager.clone();
    let log_manager3 = log_manager.clone();

    fs.dir("svc")
        .add_fidl_service(move |stream| log_manager2.spawn_log_manager(stream))
        .add_fidl_service(move |stream| log_manager3.spawn_log_sink(stream))
        .add_fidl_service(move |stream| {
            let all_archive_accessor =
                archive_accessor::ArchiveAccessor::new(all_inspect_repository.clone());
            all_archive_accessor.spawn_archive_accessor_server(stream)
        });

    fs.take_and_serve_directory_handle()?;

    let running_archivist = archive::run_archivist(archivist_state);
    let running_service_fs = fs.collect::<()>().map(Ok);
    let both = future::try_join(running_service_fs, running_archivist);
    executor.run(both, num_threads)?;
    Ok(())
}
