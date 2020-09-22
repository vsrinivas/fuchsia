// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        configs, diagnostics,
        events::types::{
            ComponentEvent, ComponentEventStream, ComponentIdentifier, DiagnosticsReadyEvent,
            EventMetadata,
        },
        repository::DiagnosticsDataRepository,
    },
    anyhow::{format_err, Error},
    chrono::prelude::*,
    fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS,
    fuchsia_inspect_contrib::{inspect_log, nodes::BoundedListNode},
    fuchsia_zircon as zx,
    futures::StreamExt,
    itertools::Itertools,
    lazy_static::lazy_static,
    log::{error, warn},
    parking_lot::{Mutex, RwLock},
    regex::Regex,
    serde::{Deserialize, Serialize},
    serde_json::Deserializer,
    std::collections::BTreeMap,
    std::ffi::{OsStr, OsString},
    std::fs,
    std::io::Write,
    std::path::{Path, PathBuf},
    std::sync::Arc,
};

// Keep only the 50 most recent events.
static INSPECT_LOG_WINDOW_SIZE: usize = 50;

/// Archive represents the top-level directory tree for the Archivist's storage.
pub struct Archive {
    /// The path to the Archive on disk.
    path: PathBuf,
}

/// Stats for a particular event group of files.
#[derive(Debug, Eq, PartialEq)]
pub struct EventFileGroupStats {
    /// The number of files associated with this group.
    pub file_count: usize,
    /// The size of those files on disk.
    pub size: u64,
}

const DATED_DIRECTORY_REGEX: &str = r"^(\d{4}-\d{2}-\d{2})$";
const EVENT_PREFIX_REGEX: &str = r"^(\d{2}:\d{2}:\d{2}.\d{3})-";
const EVENT_LOG_SUFFIX_REGEX: &str = r"event.log$";

pub type EventFileGroupStatsMap = BTreeMap<String, EventFileGroupStats>;

impl Archive {
    /// Opens an Archive at the given path, returning an error if it does not exist.
    pub fn open(path: impl Into<PathBuf>) -> Result<Self, Error> {
        let path: PathBuf = path.into();
        if path.is_dir() {
            Ok(Archive { path })
        } else {
            Err(format_err!("{} is not a directory", path.display()))
        }
    }

    /// Returns a vector of EventFileGroups and their associated stats from all dates covered by
    /// this archive.
    pub fn get_event_group_stats(&self) -> Result<EventFileGroupStatsMap, Error> {
        let mut output = EventFileGroupStatsMap::new();
        for date in self.get_dates()? {
            for group in self.get_event_file_groups(&date)? {
                let file_count = 1 + group.event_files.len();
                let size = group.size()?;
                output.insert(group.log_file_path(), EventFileGroupStats { file_count, size });
            }
        }
        Ok(output)
    }

    /// Returns a vector of the dated directory names in the archive, in sorted order.
    pub fn get_dates(&self) -> Result<Vec<String>, Error> {
        lazy_static! {
            static ref RE: Regex = Regex::new(DATED_DIRECTORY_REGEX).unwrap();
        }

        Ok(self
            .path
            .read_dir()?
            .filter_map(|file| file.ok())
            .filter_map(|entry| {
                let name = entry.file_name().into_string().unwrap_or_default();
                let is_dir = entry.file_type().and_then(|t| Ok(t.is_dir())).unwrap_or(false);
                if RE.is_match(&name) && is_dir {
                    Some(name)
                } else {
                    None
                }
            })
            .sorted()
            .collect())
    }

    /// Returns a vector of file groups in the given dated directory, in sorted order.
    pub fn get_event_file_groups(&self, date: &str) -> Result<Vec<EventFileGroup>, Error> {
        lazy_static! {
            static ref GROUP_RE: Regex = Regex::new(EVENT_PREFIX_REGEX).unwrap();
            static ref LOG_FILE_RE: Regex =
                Regex::new(&(EVENT_PREFIX_REGEX.to_owned() + EVENT_LOG_SUFFIX_REGEX)).unwrap();
        }

        Ok(self
            .path
            .join(date)
            .read_dir()?
            .filter_map(|dir_entry| dir_entry.ok())
            .filter_map(|entry| {
                let is_file = entry.metadata().and_then(|meta| Ok(meta.is_file())).unwrap_or(false);
                if !is_file {
                    return None;
                }
                let name = entry.file_name().into_string().unwrap_or_default();
                let captures = if let Some(captures) = GROUP_RE.captures(&name) {
                    captures
                } else {
                    return None;
                };
                if LOG_FILE_RE.is_match(&name) {
                    Some((captures[1].to_owned(), EventFileGroup::new(Some(entry.path()), vec![])))
                } else {
                    Some((captures[1].to_owned(), EventFileGroup::new(None, vec![entry.path()])))
                }
            })
            .sorted_by(|a, b| Ord::cmp(&a.0, &b.0))
            .group_by(|x| x.0.clone())
            .into_iter()
            .filter_map(|(_, entries)| {
                let ret = entries.map(|(_, entry)| entry).fold(
                    EventFileGroup::new(None, vec![]),
                    |mut acc, next| {
                        acc.accumulate(next);
                        acc
                    },
                );

                match ret.log_file {
                    Some(_) => Some(ret),
                    _ => None,
                }
            })
            .collect())
    }

    /// Get the path to the Archive directory.
    pub fn get_path(&self) -> &Path {
        return &self.path;
    }
}

/// Represents information about a group of event files.
#[derive(Debug, PartialEq, Eq)]
pub struct EventFileGroup {
    /// The file containing a log of events for the group.
    log_file: Option<PathBuf>,

    /// The event files referenced in the log.
    event_files: Vec<PathBuf>,
}

pub type EventError = serde_json::error::Error;

impl EventFileGroup {
    /// Constructs a new wrapper for a group of event files.
    fn new(log_file: Option<PathBuf>, event_files: Vec<PathBuf>) -> Self {
        EventFileGroup { log_file, event_files }
    }

    /// Supports folding multiple partially filled out groups together.
    fn accumulate(&mut self, other: EventFileGroup) {
        match self.log_file {
            None => {
                self.log_file = other.log_file;
            }
            _ => (),
        };

        self.event_files.extend(other.event_files.into_iter());
    }

    /// Deletes this group from disk.
    ///
    /// Returns stats on the files removed on success.
    pub fn delete(self) -> Result<EventFileGroupStats, Error> {
        let size = self.size()?;
        // There is 1 log file + each event file removed by this operation.
        let file_count = 1 + self.event_files.len();

        vec![self.log_file.unwrap()]
            .into_iter()
            .chain(self.event_files.into_iter())
            .map(|path| -> Result<(), Error> {
                fs::remove_file(&path)?;
                Ok(())
            })
            .collect::<Result<(), Error>>()?;

        Ok(EventFileGroupStats { file_count, size })
    }

    /// Gets the path to the log file for this group.
    pub fn log_file_path(&self) -> String {
        self.log_file.as_ref().expect("missing log file path").to_string_lossy().to_string()
    }

    /// Returns the size of all event files from this group on disk.
    pub fn size(&self) -> Result<u64, Error> {
        let log_file = match &self.log_file {
            None => {
                return Err(format_err!("Log file is not specified"));
            }
            Some(log_file) => log_file.clone(),
        };

        itertools::chain(&[log_file], self.event_files.iter())
            .map(|path| {
                fs::metadata(&path)
                    .or_else(|_| Err(format_err!("Failed to get size for {:?}", path)))
            })
            .map(|meta| {
                meta.and_then(|value| {
                    if value.is_file() {
                        Ok(value.len())
                    } else {
                        Err(format_err!("Path is not a file"))
                    }
                })
            })
            .fold_results(0, std::ops::Add::add)
    }

    /// Returns an iterator over the events stored in the log file.
    pub fn events(&self) -> Result<impl Iterator<Item = Result<Event, EventError>>, Error> {
        let file =
            fs::File::open(&self.log_file.as_ref().ok_or(format_err!("Log file not specified"))?)?;
        Ok(Deserializer::from_reader(file).into_iter::<Event>())
    }

    /// Returns the path to the parent directory containing this group.
    pub fn parent_directory(&self) -> Result<&Path, Error> {
        self.log_file
            .as_ref()
            .ok_or(format_err!("Log file not specified"))?
            .parent()
            .ok_or(format_err!("Log file has no parent directory"))
    }
}

/// Represents a single event in the log.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq)]
pub struct Event {
    timestamp_nanos: u64,
    event_type: String,
    relative_moniker: String,
    event_files: Vec<String>,
}

fn datetime_to_timestamp<T: TimeZone>(t: &DateTime<T>) -> u64 {
    (t.timestamp() * 1_000_000_000 + t.timestamp_subsec_nanos() as i64) as u64
}

impl Event {
    /// Create a new Event at the current time.
    pub fn new(event_type: impl ToString, relative_moniker: impl ToString) -> Self {
        Self::new_with_time(Utc::now(), event_type, relative_moniker)
    }

    /// Create a new Event at the given time.
    pub fn new_with_time<T: TimeZone>(
        time: DateTime<T>,
        event_type: impl ToString,
        relative_moniker: impl ToString,
    ) -> Self {
        Event {
            timestamp_nanos: datetime_to_timestamp(&time),
            event_type: event_type.to_string(),
            relative_moniker: relative_moniker.to_string(),
            event_files: vec![],
        }
    }

    /// Get the timestamp of this event in the given timezone.
    pub fn get_timestamp<T: TimeZone>(&self, time_zone: T) -> DateTime<T> {
        let seconds = self.timestamp_nanos / 1_000_000_000;
        let nanos = self.timestamp_nanos % 1_000_000_000;
        time_zone.timestamp(seconds as i64, nanos as u32)
    }

    /// Get the vector of event files for this event.
    pub fn get_event_files(&self) -> &Vec<String> {
        &self.event_files
    }

    /// Add an event file to this event.
    pub fn add_event_file(&mut self, file: impl AsRef<OsStr>) {
        self.event_files.push(file.as_ref().to_string_lossy().to_string());
    }
}

/// Structure that wraps an Archive and supports writing to it.
pub struct ArchiveWriter {
    /// The opened Archive to write to.
    archive: Archive,

    /// A writer for the currently opened event file group.
    open_log: EventFileGroupWriter,

    group_stats: EventFileGroupStatsMap,
}

impl ArchiveWriter {
    /// Open a directory as an archive.
    ///
    /// If the directory does not exist, it will be created.
    pub fn open(path: impl Into<PathBuf>) -> Result<Self, Error> {
        let path: PathBuf = path.into();
        if !path.exists() {
            fs::create_dir_all(&path)?;
        }

        let archive = Archive::open(&path)?;
        let open_log = EventFileGroupWriter::new(path)?;

        let mut group_stats = archive.get_event_group_stats()?;

        group_stats.remove(&open_log.get_log_file_path().to_string_lossy().to_string());
        diagnostics::set_group_stats(&group_stats);

        Ok(ArchiveWriter { archive, open_log, group_stats })
    }

    /// Get the readable Archive from this writer.
    pub fn get_archive(&self) -> &Archive {
        &self.archive
    }

    /// Get the currently opened log writer for this Archive.
    pub fn get_log(&mut self) -> &mut EventFileGroupWriter {
        &mut self.open_log
    }

    /// Rotates the log by closing the current EventFileGroup and opening a new one.
    ///
    /// Returns the name and stats for the just-closed EventFileGroup.
    pub fn rotate_log(&mut self) -> Result<(PathBuf, EventFileGroupStats), Error> {
        let mut temp_log = EventFileGroupWriter::new(self.archive.get_path())?;
        std::mem::swap(&mut self.open_log, &mut temp_log);
        temp_log.close()
    }

    fn add_group_stat(&mut self, log_file_path: &Path, stat: EventFileGroupStats) {
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

/// A writer that wraps a particular group of event files.
///
/// This struct supports writing events to the log file with associated event files through
/// |EventBuilder|.
pub struct EventFileGroupWriter {
    /// An opened file to write log events to.
    log_file: fs::File,

    /// The path to the log file.
    log_file_path: PathBuf,

    /// The path to the directory containing this file group.
    directory_path: PathBuf,

    /// The prefix for files related to this file group.
    file_prefix: String,

    /// The number of records written to the log file.
    records_written: usize,

    /// The number of bytes written for this group, including event files.
    bytes_stored: usize,

    /// The number of files written for this group, including event files.
    files_stored: usize,
}

impl EventFileGroupWriter {
    /// Create a new writable event file group.
    ///
    /// This opens or creates a dated directory in the archive and initializes a log file for the
    /// event file group.
    pub fn new(archive_path: impl AsRef<Path>) -> Result<Self, Error> {
        EventFileGroupWriter::new_with_time(Utc::now(), archive_path)
    }

    /// Creates a new writable event file group using the given time for dating the directories and
    /// files.
    fn new_with_time(time: DateTime<Utc>, archive_path: impl AsRef<Path>) -> Result<Self, Error> {
        let directory_path = archive_path.as_ref().join(time.format("%Y-%m-%d").to_string());

        fs::create_dir_all(&directory_path)?;

        let file_prefix = time.format("%H:%M:%S%.3f-").to_string();
        let log_file_path = directory_path.join(file_prefix.clone() + "event.log");
        let log_file = fs::File::create(&log_file_path)?;

        Ok(EventFileGroupWriter {
            log_file,
            log_file_path,
            directory_path,
            file_prefix,
            records_written: 0,
            bytes_stored: 0,
            files_stored: 1,
        })
    }

    /// Create a new event builder for adding an event to this group.
    pub fn new_event(
        &mut self,
        event_type: impl ToString,
        relative_moniker: impl ToString,
    ) -> EventBuilder<'_> {
        EventBuilder {
            writer: self,
            event: Event::new(event_type, relative_moniker),
            event_files: Ok(vec![]),
            event_file_size: 0,
        }
    }

    /// Gets the path to the log file for this group.
    pub fn get_log_file_path(&self) -> &Path {
        &self.log_file_path
    }

    /// Gets the stats for the event file group.
    pub fn get_stats(&self) -> EventFileGroupStats {
        EventFileGroupStats { file_count: self.files_stored, size: self.bytes_stored as u64 }
    }

    /// Write an event to the log.
    ///
    /// Returns the number of bytes written on success.
    fn write_event(&mut self, event: &Event, extra_files_size: usize) -> Result<usize, Error> {
        let value = serde_json::to_string(&event)? + "\n";
        self.log_file.write_all(value.as_ref())?;
        self.bytes_stored += value.len() + extra_files_size;
        self.records_written += 1;
        self.files_stored += event.event_files.len();
        Ok(value.len())
    }

    /// Synchronize the log with underlying storage.
    fn sync(&mut self) -> Result<(), Error> {
        Ok(self.log_file.sync_all()?)
    }

    /// Close this EventFileGroup, returning stats of what was written.
    fn close(mut self) -> Result<(PathBuf, EventFileGroupStats), Error> {
        self.sync()?;
        Ok((
            self.log_file_path,
            EventFileGroupStats { file_count: self.files_stored, size: self.bytes_stored as u64 },
        ))
    }
}

/// This struct provides a builder interface for adding event information to an individual log
/// entry before adding it to the log.
pub struct EventBuilder<'a> {
    /// The writer this is building an event for.
    writer: &'a mut EventFileGroupWriter,

    /// The partial event being built.
    event: Event,

    /// The list of event files that were created so far. If this contains Error, writing
    /// event files failed. Building will return the error.
    event_files: Result<Vec<PathBuf>, Error>,

    /// The total number of bytes written into event files.
    event_file_size: usize,
}

fn delete_files(files: &Vec<PathBuf>) -> Result<(), Error> {
    files
        .iter()
        .map(|file| -> Result<(), Error> { Ok(fs::remove_file(&file)?) })
        .fold_results((), |_, _| ())
}

impl<'a> EventBuilder<'a> {
    /// Build the event and write it to the log.
    ///
    /// Returns stats on success or the Error on failure.
    /// If this method return an error, all event files on disk will be cleaned up.
    pub fn build(mut self) -> Result<EventFileGroupStats, Error> {
        let file_count;
        if let Ok(event_files) = self.event_files.as_ref() {
            file_count = event_files.len();
            for path in event_files.iter() {
                self.event.add_event_file(
                    path.file_name().ok_or_else(|| format_err!("missing file name"))?,
                );
            }
        } else {
            return Err(self.event_files.unwrap_err());
        }

        match self.writer.write_event(&self.event, self.event_file_size) {
            Ok(bytes) => {
                Ok(EventFileGroupStats { file_count, size: (self.event_file_size + bytes) as u64 })
            }
            Err(e) => {
                self.invalidate(e);
                Err(self.event_files.unwrap_err())
            }
        }
    }

    /// Add an event file to the event.
    ///
    /// This method takes the name of a file and its contents and writes them into the archive.
    pub fn add_event_file(mut self, name: impl AsRef<OsStr>, contents: &[u8]) -> Self {
        if let Ok(file_vector) = self.event_files.as_mut() {
            let mut file_name = OsString::from(format!(
                "{}{}-",
                self.writer.file_prefix, self.writer.records_written
            ));
            file_name.push(name);
            let path = self.writer.directory_path.join(file_name);
            if let Err(e) = fs::write(&path, contents) {
                self.invalidate(Error::from(e));
            } else {
                self.event_file_size += contents.len();
                file_vector.push(path);
            }
        }

        self
    }

    /// Invalidates this EventBuilder, meaning the value will not be written to the log.
    fn invalidate(&mut self, error: Error) {
        delete_files(&self.event_files.as_ref().unwrap_or(&vec![]))
            .expect("Failed to delete files");
        self.event_files = Err(error)
    }
}

/// ArchivistState owns the tools needed to persist data
/// to the archive, as well as the service-specific repositories
/// that are populated by the archivist server and exposed in the
/// service sessions.
pub struct ArchivistState {
    /// Writer for the archive. If a path was not configured it will be `None`.
    writer: Option<ArchiveWriter>,
    log_node: BoundedListNode,
    configuration: configs::Config,
    diagnostics_repositories: Vec<Arc<RwLock<DiagnosticsDataRepository>>>,
}

impl ArchivistState {
    pub fn new(
        configuration: configs::Config,
        diagnostics_repositories: Vec<Arc<RwLock<DiagnosticsDataRepository>>>,
        writer: Option<ArchiveWriter>,
    ) -> Result<Self, Error> {
        let mut log_node = BoundedListNode::new(
            diagnostics::root().create_child("events"),
            INSPECT_LOG_WINDOW_SIZE,
        );

        inspect_log!(log_node, event: "Archivist started");

        Ok(ArchivistState { writer, log_node, configuration, diagnostics_repositories })
    }
}

async fn populate_inspect_repo(
    state: &Arc<Mutex<ArchivistState>>,
    diagnostics_ready_data: DiagnosticsReadyEvent,
) {
    // The DiagnosticsReadyEvent should always contain a directory_proxy. Its existence
    // as an Option is only to support mock objects for equality in tests.
    let diagnostics_proxy = diagnostics_ready_data.directory.unwrap();

    // TODO(55736): The pipeline specific updates should be happening asynchronously.
    // Once there is a central repository for each pipeline, the updates will just be
    // greedy selector evaluation.
    for diagnostics_repo in &state.lock().diagnostics_repositories {
        // DirectoryProxys aren't thread safe, so each repository must get
        // a unique clone.
        let identifier = diagnostics_ready_data.metadata.component_id.clone();
        match io_util::clone_directory(&diagnostics_proxy, CLONE_FLAG_SAME_RIGHTS) {
            Ok(cloned_directory) => {
                // TODO(55736): There should be a central diagnostics repository that
                // is shared across all pipelines.
                diagnostics_repo
                    .write()
                    .add_inspect_artifacts(
                        identifier.clone(),
                        diagnostics_ready_data.metadata.component_url.clone(),
                        cloned_directory,
                        diagnostics_ready_data.metadata.timestamp.clone(),
                    )
                    .unwrap_or_else(|e| {
                        warn!(
                            "Failed to add inspect artifacts for component: {:?} to repository: {:?}",
                            identifier, e
                        );
                    });
            }
            Err(e) => {
                warn!(
                    "Failed to clone diagnostics proxy of component {:?} for a repository: {:?}",
                    identifier, e
                );
            }
        }
    }
}

fn add_new_component(
    state: &Arc<Mutex<ArchivistState>>,
    identifier: ComponentIdentifier,
    component_url: String,
    event_timestamp: zx::Time,
    component_start_time: Option<zx::Time>,
) {
    for diagnostics_repo in &state.lock().diagnostics_repositories {
        diagnostics_repo
            .write()
            .add_new_component(
                identifier.clone(),
                component_url.clone(),
                event_timestamp,
                component_start_time,
            )
            .unwrap_or_else(|e| {
                error!(
                    "Failed to add new component: {:?} to repository: {:?}",
                    identifier.clone(),
                    e
                );
            });
    }
}

fn remove_from_inspect_repo(
    state: &Arc<Mutex<ArchivistState>>,
    component_id: &ComponentIdentifier,
) {
    for diagnostics_repo in &state.lock().diagnostics_repositories {
        diagnostics_repo.write().remove(&component_id);
    }
}

async fn process_event(
    state: Arc<Mutex<ArchivistState>>,
    event: ComponentEvent,
) -> Result<(), Error> {
    match event {
        ComponentEvent::Start(start) => {
            let archived_metadata = start.metadata.clone();
            add_new_component(
                &state,
                start.metadata.component_id,
                start.metadata.component_url,
                start.metadata.timestamp,
                None,
            );
            archive_event(&state, "START", archived_metadata).await
        }
        ComponentEvent::Running(running) => {
            let archived_metadata = running.metadata.clone();
            add_new_component(
                &state,
                running.metadata.component_id,
                running.metadata.component_url,
                running.metadata.timestamp,
                Some(running.component_start_time),
            );
            archive_event(&state, "RUNNING", archived_metadata.clone()).await
        }
        ComponentEvent::Stop(stop) => {
            // TODO(53939): Get inspect data from repository before removing
            // for post-mortem inspection.
            remove_from_inspect_repo(&state, &stop.metadata.component_id);
            archive_event(&state, "STOP", stop.metadata).await
        }
        ComponentEvent::DiagnosticsReady(diagnostics_ready) => {
            populate_inspect_repo(&state, diagnostics_ready).await;
            Ok(())
        }
    }
}

async fn archive_event(
    state: &Arc<Mutex<ArchivistState>>,
    _event_name: &str,
    _event_data: EventMetadata,
) -> Result<(), Error> {
    let mut state = state.lock();
    let ArchivistState { writer, log_node, configuration, .. } = &mut *state;

    let writer = if let Some(w) = writer.as_mut() {
        w
    } else {
        return Ok(());
    };

    let max_archive_size_bytes = configuration.max_archive_size_bytes;
    let max_event_group_size_bytes = configuration.max_event_group_size_bytes;

    // TODO(53939): Get inspect data from repository before removing
    // for post-mortem inspection.
    //let log = writer.get_log().new_event(event_name, event_data.component_id);
    // if let Some(data_map) = event_data.component_data_map {
    //     for (path, object) in data_map {
    //         match object {
    //             InspectData::Empty
    //             | InspectData::DeprecatedFidl(_)
    //             | InspectData::Tree(_, None) => {}
    //             InspectData::Vmo(vmo) | InspectData::Tree(_, Some(vmo)) => {
    //                 let mut contents = vec![0u8; vmo.get_size()? as usize];
    //                 vmo.read(&mut contents[..], 0)?;

    //                 // Truncate the bytes down to the last non-zero 4096-byte page of data.
    //                 // TODO(fxbug.dev/4703): Handle truncation of VMOs without reading the whole thing.
    //                 let mut last_nonzero = 0;
    //                 for (i, v) in contents.iter().enumerate() {
    //                     if *v != 0 {
    //                         last_nonzero = i;
    //                     }
    //                 }
    //                 if last_nonzero % 4096 != 0 {
    //                     last_nonzero = last_nonzero + 4096 - last_nonzero % 4096;
    //                 }
    //                 contents.resize(last_nonzero, 0);

    //                 log = log.add_event_file(path, &contents);
    //             }
    //             InspectData::File(contents) => {
    //                 log = log.add_event_file(path, &contents);
    //             }
    //         }
    //     }
    // }

    let current_group_stats = writer.get_log().get_stats();

    if current_group_stats.size >= max_event_group_size_bytes {
        let (path, stats) = writer.rotate_log()?;
        inspect_log!(log_node, event:"Rotated log",
                     new_path: path.to_string_lossy().to_string());
        writer.add_group_stat(&path, stats);
    }

    let archived_size = writer.archived_size();
    let mut current_archive_size = current_group_stats.size + archived_size;
    if current_archive_size > max_archive_size_bytes {
        let dates = writer.get_archive().get_dates().unwrap_or_else(|e| {
            warn!("Garbage collection failure");
            inspect_log!(log_node, event: "Failed to get dates for garbage collection",
                         reason: format!("{:?}", e));
            vec![]
        });

        for date in dates {
            let groups = writer.get_archive().get_event_file_groups(&date).unwrap_or_else(|e| {
                warn!("Garbage collection failure");
                inspect_log!(log_node, event: "Failed to get event file",
                             date: &date,
                             reason: format!("{:?}", e));
                vec![]
            });

            for group in groups {
                let path = group.log_file_path();
                match group.delete() {
                    Err(e) => {
                        inspect_log!(log_node, event: "Failed to remove group",
                                 path: &path,
                                 reason: format!(
                                     "{:?}", e));
                        continue;
                    }
                    Ok(stat) => {
                        current_archive_size -= stat.size;
                        writer.remove_group_stat(&PathBuf::from(&path));
                        inspect_log!(log_node, event: "Garbage collected group",
                                     path: &path,
                                     removed_files: stat.file_count as u64,
                                     removed_bytes: stat.size as u64);
                    }
                };

                if current_archive_size < max_archive_size_bytes {
                    return Ok(());
                }
            }
        }
    }

    Ok(())
}

pub async fn run_archivist(archivist_state: ArchivistState, mut events: ComponentEventStream) {
    let state = Arc::new(Mutex::new(archivist_state));

    while let Some(event) = events.next().await {
        process_event(state.clone(), event).await.unwrap_or_else(|e| {
            let mut state = state.lock();
            inspect_log!(state.log_node, event: "Failed to log event", result: format!("{:?}", e));
            error!("Failed to log event: {:?}", e);
        });
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        std::{io::Write, iter::FromIterator},
    };

    #[test]
    fn archive_open() {
        let path: PathBuf;
        {
            let dir = tempfile::tempdir().unwrap();
            path = dir.path().to_path_buf();
            assert_eq!(true, Archive::open(&path).is_ok());
        }
        assert_eq!(false, Archive::open(&path).is_ok());
    }

    #[test]
    fn archive_get_dates() {
        let dir = tempfile::tempdir().unwrap();
        fs::create_dir(dir.path().join("2019-05-08")).unwrap();
        fs::create_dir(dir.path().join("2019-04-10")).unwrap();
        fs::create_dir(dir.path().join("2019-04-incorrect-format")).unwrap();
        // Create a file with the correct format. It will not be included since it is not a
        // directory.
        fs::File::create(dir.path().join("2019-05-09")).unwrap();

        let archive = Archive::open(dir.path()).unwrap();
        assert_eq!(
            vec!["2019-04-10".to_string(), "2019-05-08".to_string()],
            archive.get_dates().unwrap()
        );
    }

    fn write_test_to_file<T: AsRef<Path>>(path: T) {
        let mut file = fs::File::create(path).expect("failed to create file");
        write!(file, "test").expect("failed to write file");
        file.sync_all().expect("failed to sync file");
    }

    #[test]
    fn archive_get_file_groups() {
        let dir = tempfile::tempdir().unwrap();
        let dated_dir_path = dir.path().join("2019-05-08");
        fs::create_dir(&dated_dir_path).unwrap();

        // Event group with only log file.
        let event_log_file_name = dated_dir_path.join("10:30:00.000-event.log");
        write_test_to_file(&event_log_file_name);

        // Event group with event files.
        let aux_event_log_file_name = dated_dir_path.join("11:30:00.000-event.log");
        let aux_file_1 = dated_dir_path.join("11:30:00.000-aux_file_1.info");
        let aux_file_2 = dated_dir_path.join("11:30:00.000-aux_file_2.info");
        write_test_to_file(&aux_event_log_file_name);
        write_test_to_file(&aux_file_1);
        write_test_to_file(&aux_file_2);

        // Event group missing log file (invalid).
        fs::File::create(dated_dir_path.join("12:30:00.000-aux_file_1.info")).unwrap();
        fs::File::create(dated_dir_path.join("12:30:00.000-aux_file_2.info")).unwrap();

        // Name does not match pattern (invalid).
        fs::File::create(dated_dir_path.join("13:30:00-event.log")).unwrap();

        // Directory rather than file (invalid).
        fs::create_dir(dated_dir_path.join("14:30:00.000-event.log")).unwrap();

        let archive = Archive::open(dir.path()).unwrap();
        assert_eq!(
            vec![
                EventFileGroup::new(Some(event_log_file_name.clone()), vec![]),
                EventFileGroup::new(
                    Some(aux_event_log_file_name.clone()),
                    vec![aux_file_1.clone(), aux_file_2.clone()]
                )
            ],
            archive.get_event_file_groups("2019-05-08").unwrap()
        );

        assert_eq!(
            BTreeMap::from_iter(
                vec![
                    (
                        event_log_file_name.to_string_lossy().to_string(),
                        EventFileGroupStats { file_count: 1, size: 4 }
                    ),
                    (
                        aux_event_log_file_name.to_string_lossy().to_string(),
                        EventFileGroupStats { file_count: 3, size: 4 * 3 }
                    )
                ]
                .into_iter()
            ),
            archive.get_event_group_stats().unwrap()
        );

        for group in archive.get_event_file_groups("2019-05-08").unwrap() {
            group.delete().unwrap();
        }

        assert_eq!(0, archive.get_event_file_groups("2019-05-08").unwrap().len());

        // Open an empty directory.
        fs::create_dir(dir.path().join("2019-05-07")).unwrap();
        assert_eq!(0, archive.get_event_file_groups("2019-05-07").unwrap().len());

        // Open a missing directory
        assert_eq!(true, archive.get_event_file_groups("2019-05-06").is_err());
    }

    #[test]
    fn event_file_group_size() {
        let dir = tempfile::tempdir().unwrap();
        write_test_to_file(dir.path().join("a"));
        write_test_to_file(dir.path().join("b"));
        write_test_to_file(dir.path().join("c"));

        assert_eq!(
            12,
            EventFileGroup::new(
                Some(dir.path().join("a")),
                vec![dir.path().join("b"), dir.path().join("c")]
            )
            .size()
            .expect("failed to get size")
        );

        assert_eq!(
            4,
            EventFileGroup::new(Some(dir.path().join("a")), vec![],)
                .size()
                .expect("failed to get size")
        );

        // Absent file "d" causes error.
        assert_eq!(
            true,
            EventFileGroup::new(Some(dir.path().join("a")), vec![dir.path().join("d")],)
                .size()
                .is_err()
        );

        // Missing log file.
        assert_eq!(true, EventFileGroup::new(None, vec![dir.path().join("b")],).size().is_err());

        // Log file is actually a directory.
        assert_eq!(
            true,
            EventFileGroup::new(Some(dir.path().to_path_buf()), vec![dir.path().join("b")],)
                .size()
                .is_err()
        );
    }

    #[test]
    fn event_creation() {
        let time = Utc::now();
        let event = Event::new_with_time(time, "START", "a/b/component.cmx:1234");
        assert_eq!(time, event.get_timestamp(Utc));
        assert_eq!(
            Event {
                timestamp_nanos: datetime_to_timestamp(&time),
                relative_moniker: "a/b/component.cmx:1234".into(),
                event_type: "START".to_string(),
                event_files: vec![],
            },
            event
        );
    }

    #[test]
    fn event_ordering() {
        let event1 = Event::new("START", "a/b/c.cmx:123");
        let event2 = Event::new("END", "a/b/c.cmx:123");
        assert!(
            event1.get_timestamp(Utc) < event2.get_timestamp(Utc),
            "Expected {:?} before {:?}",
            event1.get_timestamp(Utc),
            event2.get_timestamp(Utc)
        );
    }

    #[test]
    fn event_event_files() {
        let mut event = Event::new("START", "a/b/c.cmx:123");
        event.add_event_file("f1");
        assert_eq!(&vec!["f1"], event.get_event_files());
    }

    #[test]
    fn event_file_group_writer() {
        let dir = tempfile::tempdir().unwrap();
        let mut writer = EventFileGroupWriter::new_with_time(
            Utc.ymd(2019, 05, 08).and_hms_milli(12, 30, 14, 31),
            dir.path(),
        )
        .expect("failed to create writer");
        assert!(writer.sync().is_ok());

        let meta = fs::metadata(dir.path().join("2019-05-08"));
        assert!(meta.is_ok());
        assert!(meta.unwrap().is_dir());

        let meta = fs::metadata(dir.path().join("2019-05-08").join("12:30:14.031-event.log"));
        assert!(meta.is_ok());
        assert!(meta.unwrap().is_file());

        assert!(writer.new_event("START", "a/b/test.cmx:0").build().is_ok());
        assert!(writer
            .new_event("EXIT", "a/b/test.cmx:0")
            .add_event_file("root.inspect", b"INSP TEST")
            .build()
            .is_ok());

        let extra_file_path = dir.path().join("2019-05-08").join("12:30:14.031-1-root.inspect");
        let meta = fs::metadata(&extra_file_path);
        assert!(meta.is_ok());
        assert!(meta.unwrap().is_file());
        assert_eq!("INSP TEST", fs::read_to_string(&extra_file_path).unwrap());
    }

    #[test]
    fn archive_writer() {
        let dir = tempfile::tempdir().unwrap();
        let mut archive =
            ArchiveWriter::open(dir.path().join("archive")).expect("failed to create archive");

        archive
            .get_log()
            .new_event("START", "a/b/test.cmx:0")
            .build()
            .expect("failed to write log");
        archive
            .get_log()
            .new_event("STOP", "a/b/test.cmx:0")
            .add_event_file("root.inspect", b"test")
            .build()
            .expect("failed to write log");

        let mut events = vec![];
        archive.get_archive().get_dates().unwrap().into_iter().for_each(|date| {
            archive.get_archive().get_event_file_groups(&date).unwrap().into_iter().for_each(
                |group| {
                    group.events().unwrap().for_each(|event| {
                        events.push(event.unwrap());
                    })
                },
            );
        });

        assert_eq!(2, events.len());

        let (_, stats) = archive.rotate_log().unwrap();
        assert_eq!(2, stats.file_count);
        assert_ne!(0, stats.size);

        let mut group_count = 0;
        archive.get_archive().get_dates().unwrap().into_iter().for_each(|date| {
            group_count += archive.get_archive().get_event_file_groups(&date).unwrap().len();
        });
        assert_eq!(2, group_count);

        let mut stats = archive
            .get_log()
            .new_event("STOP", "a/b/test.cmx:0")
            .add_event_file("root.inspect", b"test")
            .build()
            .expect("failed to write log");

        // Check the stats returned by the log; we add one to the file count for the log file
        // itself.
        stats.file_count += 1;
        assert_eq!(stats, archive.get_log().get_stats());
    }
}
