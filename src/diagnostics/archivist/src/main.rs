// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![allow(dead_code)]

use {
    failure::Error,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{component, health::Reporter},
    fuchsia_inspect_contrib::{inspect_log, nodes::BoundedListNode},
    fuchsia_syslog::{self, macros::*},
    futures::{future, FutureExt, StreamExt},
    io_util,
    std::path::{Path, PathBuf},
    std::sync::{Arc, Mutex},
};

mod archive;
mod collection;
mod configs;
mod diagnostics;

static ARCHIVE_PATH: &str = "/data/archive";
static ARCHIVE_CONFIG_FILE: &str = "/config/data/archivist_config.json";

static DEFAULT_NUM_THREADS: usize = 4;

// Keep only the 50 most recent events.
static INSPECT_LOG_WINDOW_SIZE: usize = 50;

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new()?;

    diagnostics::init();
    fx_log_info!("Archivist is starting up...");

    // Ensure that an archive exists.
    std::fs::create_dir_all(ARCHIVE_PATH).expect("failed to initialize archive");

    let mut fs = ServiceFs::new();
    diagnostics::export(&mut fs);
    fs.add_remote(
        "archive",
        io_util::open_directory_in_namespace(
            ARCHIVE_PATH,
            io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
        )?,
    );
    fs.take_and_serve_directory_handle()?;

    let archivist_configuration: configs::Config = match configs::parse_config(ARCHIVE_CONFIG_FILE)
    {
        Ok(config) => config,
        Err(parsing_error) => panic!("Parsing configuration failed: {}", parsing_error),
    };

    let archivist_threads: usize =
        archivist_configuration.num_threads.unwrap_or(DEFAULT_NUM_THREADS);

    executor.run(
        future::try_join(fs.collect::<()>().map(Ok), run_archivist(archivist_configuration)),
        archivist_threads,
    )?;
    Ok(())
}

struct ArchivistState {
    writer: archive::ArchiveWriter,
    group_stats: archive::EventFileGroupStatsMap,
    log_node: BoundedListNode,
    configuration: configs::Config,
}

impl ArchivistState {
    fn new(configuration: configs::Config) -> Result<Self, Error> {
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

        Ok(ArchivistState { writer, group_stats, log_node, configuration })
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

async fn run_archivist(archivist_configuration: configs::Config) -> Result<(), Error> {
    let state = Arc::new(Mutex::new(ArchivistState::new(archivist_configuration)?));
    component::health().set_starting_up();

    let mut collector = collection::HubCollector::new("/hub")?;
    let mut events = collector.component_events().unwrap();

    let collector_state = state.clone();
    fasync::spawn(collector.start().then(|e| {
        async move {
            let mut state = collector_state.lock().unwrap();
            component::health().set_unhealthy("Collection loop stopped");
            inspect_log!(state.log_node, event: "Collection ended", result: format!("{:?}", e));
            fx_log_err!("Collection ended with result {:?}", e);
        }
    }));

    component::health().set_ok();

    while let Some(event) = events.next().await {
        let state = state.clone();
        process_event(state.clone(), event).await.unwrap_or_else(|e| {
            let mut state = state.lock().unwrap();
            inspect_log!(state.log_node, event: "Failed to log event", result: format!("{:?}", e));
            fx_log_err!("Failed to log event: {:?}", e);
        });
    }

    Ok(())
}

async fn process_event(
    state: Arc<Mutex<ArchivistState>>,
    event: collection::ComponentEvent,
) -> Result<(), Error> {
    let mut state = state.lock().unwrap();

    let (event_name, event_data) = match event {
        collection::ComponentEvent::Existing(data) => ("EXISTING", data),
        collection::ComponentEvent::Start(data) => ("START", data),
        collection::ComponentEvent::Stop(data) => ("STOP", data),
    };

    let mut log = state.writer.get_log().new_event(
        event_name,
        event_data.component_name,
        event_data.component_id,
    );

    if let Some(extra) = event_data.extra_data {
        for (key, object) in extra {
            match object {
                collection::ExtraData::Empty => {}
                collection::ExtraData::Vmo(vmo) => {
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

                    log = log.add_event_file(key, &contents);
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
            fx_log_err!("Garbage collection failure");
            inspect_log!(state.log_node, event: "Failed to get dates for garbage collection",
                         reason: format!("{:?}", e));
            vec![]
        });

        for date in dates {
            let groups =
                state.writer.get_archive().get_event_file_groups(&date).unwrap_or_else(|e| {
                    fx_log_err!("Garbage collection failure");
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
