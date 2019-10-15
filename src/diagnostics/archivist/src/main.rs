// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use {
    failure::Error,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_diagnostics_inspect::Selector,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{component, health::Reporter},
    fuchsia_inspect_contrib::{inspect_log, nodes::BoundedListNode},
    futures::{future, FutureExt, StreamExt},
    io_util,
    std::path::{Path, PathBuf},
    std::sync::{Arc, Mutex},
};

mod archive;
mod collection;
mod configs;
mod diagnostics;
mod inspect;
mod logs;
mod selector_evaluator;
mod selectors;

static ARCHIVE_PATH: &str = "/data/archive";
static INSPECT_ALL_SELECTORS: &str = "/config/data/pipelines/all/";
static ARCHIVE_CONFIG_FILE: &str = "/config/data/archivist_config.json";

static DEFAULT_NUM_THREADS: usize = 4;

// Keep only the 50 most recent events.
static INSPECT_LOG_WINDOW_SIZE: usize = 50;

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new()?;

    diagnostics::init();

    // Ensure that an archive exists.
    std::fs::create_dir_all(ARCHIVE_PATH).expect("failed to initialize archive");

    let opt = logs::Opt::from_args();
    let log_manager = logs::LogManager::new();
    if !opt.disable_klog {
        log_manager.spawn_klogger()?;
    }

    let mut fs = ServiceFs::new();
    diagnostics::export(&mut fs);
    fs.add_remote(
        "archive",
        io_util::open_directory_in_namespace(
            ARCHIVE_PATH,
            io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
        )?,
    );

    // Publish stats on global storage.
    fs.add_remote(
        "global_data",
        storage_inspect_proxy("global_data".to_string(), PathBuf::from("/global_data"))?,
    );
    fs.add_remote(
        "global_tmp",
        storage_inspect_proxy("global_tmp".to_string(), PathBuf::from("/global_tmp"))?,
    );

    let all_selectors: Vec<Arc<Selector>> = match selectors::parse_selectors(INSPECT_ALL_SELECTORS)
    {
        Ok(selectors) => selectors.into_iter().map(|selector| Arc::new(selector)).collect(),
        Err(parsing_error) => panic!("Parsing selectors failed: {}", parsing_error),
    };

    // The repository that will serve as the data transfer between the archivist server
    // and all services needing access to inspect data.
    let all_inspect_repository =
        Arc::new(Mutex::new(inspect::InspectDataRepository::new(all_selectors)));

    let archivist_configuration: configs::Config = match configs::parse_config(ARCHIVE_CONFIG_FILE)
    {
        Ok(config) => config,
        Err(parsing_error) => panic!("Parsing configuration failed: {}", parsing_error),
    };

    let archivist_threads: usize =
        archivist_configuration.num_threads.unwrap_or(DEFAULT_NUM_THREADS);

    let archivist_state =
        ArchivistState::new(archivist_configuration, all_inspect_repository.clone())?;

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

    fs.take_and_serve_directory_handle()?;

    executor.run(
        future::try_join(fs.collect::<()>().map(Ok), run_archivist(archivist_state)),
        archivist_threads,
    )?;
    Ok(())
}

/// ArchivistState owns the tools needed to persist data
/// to the archive, as well as the service-specific repositories
/// that are populated by the archivist server and exposed in the
/// service sessions.
struct ArchivistState {
    writer: archive::ArchiveWriter,
    group_stats: archive::EventFileGroupStatsMap,
    log_node: BoundedListNode,
    configuration: configs::Config,
    inspect_repository: Arc<Mutex<inspect::InspectDataRepository>>,
}

impl ArchivistState {
    fn new(
        configuration: configs::Config,
        inspect_repository: Arc<Mutex<inspect::InspectDataRepository>>,
    ) -> Result<Self, Error> {
        let mut writer = archive::ArchiveWriter::open(ARCHIVE_PATH)?;
        let mut group_stats = writer.get_archive().get_event_group_stats()?;

        let log = writer.get_log();
        diagnostics::set_current_group(log.get_log_file_path(), &log.get_stats());

        // Do not include the current group in the stats.
        group_stats.remove(&log.get_log_file_path().to_string_lossy().to_string());

        diagnostics::set_group_stats(&group_stats);

        for (_, group_stat) in &group_stats {
            diagnostics::add_stats(&group_stat);
        }

        // Add the counts for the current group.
        diagnostics::add_stats(&log.get_stats());

        let mut log_node = BoundedListNode::new(
            diagnostics::root().create_child("events"),
            INSPECT_LOG_WINDOW_SIZE,
        );

        inspect_log!(log_node, event: "Archivist started");

        Ok(ArchivistState { writer, group_stats, log_node, configuration, inspect_repository })
    }

    fn add_group_stat(&mut self, log_file_path: &Path, stat: archive::EventFileGroupStats) {
        self.group_stats.insert(log_file_path.to_string_lossy().to_string(), stat);
        diagnostics::set_group_stats(&self.group_stats);
    }

    fn remove_group_stat(&mut self, log_file_path: &Path) {
        self.group_stats.remove(&log_file_path.to_string_lossy().to_string());
        diagnostics::set_group_stats(&self.group_stats);
    }

    fn archived_size(&self) -> u64 {
        let mut ret = 0;
        for (_, v) in &self.group_stats {
            ret += v.size;
        }
        ret
    }
}

async fn run_archivist(archivist_state: ArchivistState) -> Result<(), Error> {
    let state = Arc::new(Mutex::new(archivist_state));
    component::health().set_starting_up();

    let mut collector = collection::HubCollector::new("/hub")?;
    let mut events = collector.component_events().unwrap();

    let collector_state = state.clone();
    fasync::spawn(collector.start().then(|e| {
        async move {
            let mut state = collector_state.lock().unwrap();
            component::health().set_unhealthy("Collection loop stopped");
            inspect_log!(state.log_node, event: "Collection ended", result: format!("{:?}", e));
            eprintln!("Collection ended with result {:?}", e);
        }
    }));

    component::health().set_ok();

    while let Some(event) = events.next().await {
        let state = state.clone();
        process_event(state.clone(), event).await.unwrap_or_else(|e| {
            let mut state = state.lock().unwrap();
            inspect_log!(state.log_node, event: "Failed to log event", result: format!("{:?}", e));
            eprintln!("Failed to log event: {:?}", e);
        });
    }

    Ok(())
}

async fn archive_event(
    state: &mut Arc<Mutex<ArchivistState>>,
    event_name: &str,
    event_data: collection::ComponentEventData,
) -> Result<(), Error> {
    let mut state = state.lock().unwrap();

    let mut log = state.writer.get_log().new_event(
        event_name,
        event_data.component_name,
        event_data.component_id,
    );

    if let Some(data_map) = event_data.component_data_map {
        for (path, object) in data_map {
            match object {
                collection::Data::Empty => {}
                collection::Data::Vmo(vmo) => {
                    let mut contents = vec![0u8; vmo.get_size()? as usize];
                    vmo.read(&mut contents[..], 0)?;

                    // Truncate the bytes down to the last non-zero 4096-byte page of data.
                    // TODO(CF-830): Handle truncation of VMOs without reading the whole thing.
                    let mut last_nonzero = 0;
                    for (i, v) in contents.iter().enumerate() {
                        if *v != 0 {
                            last_nonzero = i;
                        }
                    }
                    if last_nonzero % 4096 != 0 {
                        last_nonzero = last_nonzero + 4096 - last_nonzero % 4096;
                    }
                    contents.resize(last_nonzero, 0);

                    log = log.add_event_file(path, &contents);
                }
            }
        }
    }

    let event_stat = log.build()?;
    let current_group_stats = state.writer.get_log().get_stats();
    diagnostics::update_current_group(&current_group_stats);
    diagnostics::add_stats(&event_stat);

    if current_group_stats.size >= state.configuration.max_event_group_size_bytes {
        let (path, stats) = state.writer.rotate_log()?;
        inspect_log!(state.log_node, event:"Rotated log",
                     new_path: path.to_string_lossy().to_string());
        let log = state.writer.get_log();
        diagnostics::set_current_group(log.get_log_file_path(), &log.get_stats());
        diagnostics::add_stats(&log.get_stats());
        state.add_group_stat(&path, stats);
    }

    let mut current_archive_size = current_group_stats.size + state.archived_size();
    if current_archive_size > state.configuration.max_archive_size_bytes {
        let dates = state.writer.get_archive().get_dates().unwrap_or_else(|e| {
            eprintln!("Garbage collection failure");
            inspect_log!(state.log_node, event: "Failed to get dates for garbage collection",
                         reason: format!("{:?}", e));
            vec![]
        });

        for date in dates {
            let groups =
                state.writer.get_archive().get_event_file_groups(&date).unwrap_or_else(|e| {
                    eprintln!("Garbage collection failure");
                    inspect_log!(state.log_node, event: "Failed to get event file",
                             date: &date,
                             reason: format!("{:?}", e));
                    vec![]
                });

            for group in groups {
                let path = group.log_file_path();
                match group.delete() {
                    Err(e) => {
                        inspect_log!(state.log_node, event: "Failed to remove group",
                                 path: &path,
                                 reason: format!(
                                     "{:?}", e));
                        continue;
                    }
                    Ok(stat) => {
                        current_archive_size -= stat.size;
                        diagnostics::subtract_stats(&stat);
                        state.remove_group_stat(&PathBuf::from(&path));
                        inspect_log!(state.log_node, event: "Garbage collected group",
                                     path: &path,
                                     removed_files: stat.file_count as u64,
                                     removed_bytes: stat.size as u64);
                    }
                };

                if current_archive_size < state.configuration.max_archive_size_bytes {
                    return Ok(());
                }
            }
        }
    }

    Ok(())
}

fn populate_inspect_repo(
    state: &mut Arc<Mutex<ArchivistState>>,
    inspect_reader_data: collection::InspectReaderData,
) -> Result<(), Error> {
    let state = state.lock().unwrap();
    let mut inspect_repo = state.inspect_repository.lock().unwrap();

    // The InspectReaderData should always contain a directory_proxy. Its existence
    // as an Option is only to support mock objects for equality in tests.
    let inspect_directory_proxy = inspect_reader_data.data_directory_proxy.unwrap();

    inspect_repo.add(
        inspect_reader_data.component_name,
        inspect_reader_data.absolute_moniker,
        inspect_reader_data.component_hierarchy_path,
        inspect_directory_proxy,
    )
}

async fn process_event(
    mut state: Arc<Mutex<ArchivistState>>,
    event: collection::ComponentEvent,
) -> Result<(), Error> {
    match event {
        collection::ComponentEvent::Existing(data) => {
            return archive_event(&mut state, "EXISTING", data).await
        }
        collection::ComponentEvent::Start(data) => {
            return archive_event(&mut state, "START", data).await
        }
        collection::ComponentEvent::Stop(data) => {
            return archive_event(&mut state, "STOP", data).await
        }
        collection::ComponentEvent::OutDirectoryAppeared(data) => {
            return populate_inspect_repo(&mut state, data)
        }
    };
}

// Returns a DirectoryProxy that contains a dynamic inspect file with stats on files stored under
// `path`.
fn storage_inspect_proxy(name: String, path: PathBuf) -> Result<DirectoryProxy, Error> {
    let (proxy, server) =
        create_proxy::<DirectoryMarker>().expect("failed to create directoryproxy");

    fasync::spawn(async move {
        diagnostics::publish_data_directory_stats(name, path, server.into_channel().into()).await;
    });

    Ok(proxy)
}
