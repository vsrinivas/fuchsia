// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Archivist collects and stores diagnostic data from components.

#![warn(missing_docs)]

use {
    archivist_lib::{archive, configs, data_stats, diagnostics, inspect, logs},
    failure::Error,
    fidl_fuchsia_diagnostics_inspect::Selector,
    fidl_fuchsia_io::DirectoryProxy,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::{future, FutureExt, StreamExt},
    io_util, selectors,
    std::sync::{Arc, RwLock},
};

static INSPECT_ALL_SELECTORS: &str = "/config/data/pipelines/all/";
static ARCHIVE_CONFIG_FILE: &str = "/config/data/archivist_config.json";

static DEFAULT_NUM_THREADS: usize = 4;

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new()?;

    diagnostics::init();

    // Ensure that an archive exists.
    std::fs::create_dir_all(archive::ARCHIVE_PATH).expect("failed to initialize archive");

    let opt = logs::Opt::from_args();
    let log_manager = logs::LogManager::new();
    if !opt.disable_klog {
        log_manager.spawn_klogger()?;
    }

    let mut fs = ServiceFs::new();
    diagnostics::serve(&mut fs)?;
    fs.add_remote(
        "archive",
        io_util::open_directory_in_namespace(
            archive::ARCHIVE_PATH,
            io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
        )?,
    );

    let all_selectors: Vec<Arc<Selector>> = match selectors::parse_selectors(INSPECT_ALL_SELECTORS)
    {
        Ok(selectors) => selectors.into_iter().map(|selector| Arc::new(selector)).collect(),
        Err(parsing_error) => panic!("Parsing selectors failed: {}", parsing_error),
    };

    // The repository that will serve as the data transfer between the archivist server
    // and all services needing access to inspect data.
    let all_inspect_repository =
        Arc::new(RwLock::new(inspect::InspectDataRepository::new(all_selectors)));

    let archivist_configuration: configs::Config = match configs::parse_config(ARCHIVE_CONFIG_FILE)
    {
        Ok(config) => config,
        Err(parsing_error) => panic!("Parsing configuration failed: {}", parsing_error),
    };

    let archivist_threads: usize =
        archivist_configuration.num_threads.unwrap_or(DEFAULT_NUM_THREADS);

    if let Some(to_summarize) = &archivist_configuration.summarized_dirs {
        data_stats::add_stats_nodes(&mut fs, to_summarize)?;
    }

    let archivist_state =
        archive::ArchivistState::new(archivist_configuration, all_inspect_repository.clone())?;

    let log_manager2 = log_manager.clone();
    let log_manager3 = log_manager.clone();

    fs.dir("svc")
        .add_fidl_service(move |stream| log_manager2.spawn_log_manager(stream))
        .add_fidl_service(move |stream| log_manager3.spawn_log_sink(stream))
        .add_fidl_service(move |stream| {
            // Every reader session gets their own clone of the inspect repository.
            // This ends up functioning like a SPMC channel, in which only the archivist
            // pushes updates to the repository, but multiple inspect Reader sessions
            // read the data.
            let reader_inspect_repository = all_inspect_repository.clone();
            let inspect_reader_server = inspect::ReaderServer::new(reader_inspect_repository);

            inspect_reader_server.spawn_reader_server(stream)
        });

    let (out_dir_raw, out_dir_remote) = zx::Channel::create()?;
    let out_dir = DirectoryProxy::new(fasync::Channel::from_channel(out_dir_raw)?);
    fs.serve_connection(out_dir_remote)?;
    fs.take_and_serve_directory_handle()?;

    let running_archivist = archive::run_archivist(archivist_state, out_dir);
    let running_service_fs = fs.collect::<()>().map(Ok);
    let both = future::try_join(running_service_fs, running_archivist);
    executor.run(both, archivist_threads)?;
    Ok(())
}
