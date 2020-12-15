// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    async_std::{
        fs::{create_dir_all, read_dir, remove_file, File, OpenOptions},
        io::{BufReader, Lines},
        path::PathBuf,
        prelude::*,
        stream::{Stream, StreamExt},
        sync::{RwLock, RwLockWriteGuard},
        task::Poll,
    },
    async_trait::async_trait,
    diagnostics_data::{LogsData, Timestamp},
    ffx_config::get,
    futures::TryStreamExt,
    serde::{Deserialize, Serialize},
    std::convert::TryInto,
    std::io::ErrorKind,
    std::pin::Pin,
};

const CACHE_DIRECTORY_CONFIG: &str = "proactive_log.cache_directory";
const MAX_LOG_SIZE_CONFIG: &str = "proactive_log.max_log_size_bytes";
const MAX_SESSION_SIZE_CONFIG: &str = "proactive_log.max_session_size_bytes";

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum EventType {
    LoggingStarted,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum LogData {
    TargetLog(LogsData),
    MalformedTargetLog(String),
    FfxEvent(EventType),
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct LogEntry {
    pub data: LogData,
    pub timestamp: Timestamp,
    pub version: u64,
}

struct LogFileEntries {
    lines: Lines<BufReader<Box<File>>>,
}

impl LogFileEntries {
    fn new(lines: Lines<BufReader<Box<File>>>) -> Self {
        Self { lines }
    }
}

impl Stream for LogFileEntries {
    type Item = Result<LogEntry>;

    fn poll_next(
        mut self: std::pin::Pin<&mut Self>,
        cx: &mut futures::task::Context<'_>,
    ) -> Poll<Option<Self::Item>> {
        Pin::new(&mut self.lines).poll_next(cx).map(|line_opt| {
            line_opt.map(|line| match line {
                Ok(line) => serde_json::from_str(&line).map_err(|e| anyhow!(e)),
                Err(e) => Err(anyhow!(e)),
            })
        })
    }
}

#[derive(Debug)]
struct LogFile {
    path: PathBuf,
    file: Option<Box<File>>,
}

impl LogFile {
    fn new(path: PathBuf) -> Self {
        Self { path, file: None }
    }

    fn from_file(path: PathBuf, file: Box<File>) -> Self {
        Self { path, file: Some(file) }
    }

    async fn sort_directory(parent: &PathBuf) -> Result<Vec<LogFile>> {
        let mut reader = read_dir(parent.clone()).await?;
        let mut result = vec![];
        while let Some(ent) = reader.try_next().await? {
            let fname_num: u64 = match String::from(ent.file_name().to_string_lossy()).parse() {
                Ok(name) => name,
                Err(_) => continue,
            };
            result.push((fname_num, ent));
        }

        result.sort_by_key(|tup| tup.0);

        Ok(result.iter().map(|tup| LogFile::new(tup.1.path())).collect())
    }

    async fn create(parent: &PathBuf) -> Result<Self> {
        let mut file_path = parent.clone();
        let fname = match Self::sort_directory(parent).await?.last() {
            Some(f) => f.parsed_name()? + 1,
            None => 1,
        };

        file_path.push(fname.to_string());

        let mut options = OpenOptions::new();
        options.create(true).write(true);

        let f = options
            .open(file_path.clone())
            .await
            .context("opening output file")
            .map(|f| Box::new(f))?;

        Ok(Self::from_file(file_path, f))
    }

    fn parsed_name(&self) -> Result<u64> {
        Ok(String::from(
            self.path
                .file_name()
                .ok_or(anyhow!("invalid path name {:?}", &self.path.to_str()))?
                .to_string_lossy(),
        )
        .parse()?)
    }

    async fn get_file(&mut self) -> Result<&mut Box<File>> {
        if self.file.is_none() {
            let mut options = OpenOptions::new();
            options.append(true);
            self.file = Some(
                options
                    .open(self.path.clone())
                    .await
                    .context("opening output file")
                    .map(|f| Box::new(f))?,
            );
        }

        Ok(self.file.as_mut().unwrap())
    }

    async fn bytes_in_file(&mut self) -> Result<usize> {
        Ok(self.get_file().await?.metadata().await?.len().try_into()?)
    }

    async fn remove(&mut self) -> Result<()> {
        remove_file(self.path.clone()).await?;
        self.file = None;
        Ok(())
    }

    async fn write_entries(&mut self, entries: Vec<&'_ Vec<u8>>) -> Result<()> {
        let f = self.get_file().await?;

        for entry in entries.iter() {
            f.write_all(entry.as_slice()).await?;
        }
        f.sync_data().await?;

        Ok(())
    }

    async fn iter_entries(&self) -> Result<LogFileEntries> {
        Ok(LogFileEntries::new(
            BufReader::new(Box::new(File::open(self.path.clone()).await?)).lines(),
        ))
    }
}

#[derive(Debug, Default)]
struct DiagnosticsStreamerInner {
    target_dir: Option<PathBuf>,
    current_file: Option<LogFile>,
    max_file_size_bytes: usize,
    max_session_size_bytes: usize,
}

impl Clone for DiagnosticsStreamerInner {
    fn clone(&self) -> Self {
        Self {
            target_dir: self.target_dir.clone(),
            current_file: None,
            max_file_size_bytes: self.max_file_size_bytes,
            max_session_size_bytes: self.max_session_size_bytes,
        }
    }
}

pub struct DiagnosticsStreamer {
    inner: RwLock<DiagnosticsStreamerInner>,
}

impl Clone for DiagnosticsStreamer {
    fn clone(&self) -> Self {
        let inner = futures::executor::block_on(self.inner.read());
        Self { inner: RwLock::new(inner.clone()) }
    }
}

impl Default for DiagnosticsStreamer {
    fn default() -> Self {
        Self { inner: RwLock::new(DiagnosticsStreamerInner::default()) }
    }
}

#[async_trait]
pub trait GenericDiagnosticsStreamer {
    async fn setup_stream(
        &self,
        target_nodename: String,
        target_boot_time_nanos: u64,
    ) -> Result<()>;

    async fn append_logs(&self, entries: Vec<LogEntry>) -> Result<()>;

    async fn read_most_recent_timestamp(&self) -> Result<Option<Timestamp>>;
}

#[async_trait::async_trait]
impl GenericDiagnosticsStreamer for DiagnosticsStreamer {
    async fn setup_stream(
        &self,
        target_nodename: String,
        target_boot_time_nanos: u64,
    ) -> Result<()> {
        self.setup_stream_with_config(
            target_nodename,
            target_boot_time_nanos,
            get::<std::path::PathBuf, &str>(CACHE_DIRECTORY_CONFIG).await?.into(),
            get::<u64, &str>(MAX_LOG_SIZE_CONFIG).await? as usize,
            get::<u64, &str>(MAX_SESSION_SIZE_CONFIG).await? as usize,
        )
        .await
    }

    async fn append_logs(&self, entries: Vec<LogEntry>) -> Result<()> {
        let entries = entries
            .iter()
            .filter_map(|entry| match serde_json::to_string(entry) {
                Ok(s) => {
                    let mut owned = s.clone();
                    owned.push('\n');
                    Some(owned.into_bytes())
                }
                Err(e) => {
                    log::warn!("failed to serialize LogEntry: {:?}. Log was: {:?}", e, &entry);
                    None
                }
            })
            .collect::<Vec<_>>();

        let mut inner = self.inner.write().await;
        let max_file_size = inner.max_file_size_bytes;
        let parent_path = inner.target_dir.as_ref().ok_or(anyhow!("no stream setup!"))?.clone();

        let mut file = match inner.current_file.take() {
            Some(f) => f,
            None => {
                // Continue appending to the latest file for this session if one exists.
                let latest_file =
                    LogFile::sort_directory(&parent_path).await?.into_iter().rev().next();
                match latest_file {
                    Some(f) => f,
                    None => LogFile::create(&parent_path).await?,
                }
            }
        };

        let mut buf_bytes = file.bytes_in_file().await?;
        let mut buf: Vec<&Vec<u8>> = vec![];

        for log_bytes in entries.as_slice() {
            // We flush the buffer to disk if this log entry would push us over the max file size
            // or if this log entry is, alone, larger the max file size (rather than discard the log entry)
            if !buf.is_empty()
                && (buf_bytes + log_bytes.len() > max_file_size || log_bytes.len() > max_file_size)
            {
                file.write_entries(buf).await?;
                buf_bytes = 0;
                buf = vec![];

                file = LogFile::create(&parent_path).await?;
            }

            buf_bytes += log_bytes.len();
            buf.push(log_bytes);
        }

        if !buf.is_empty() {
            // We don't need to create a new file here in the special case that
            // the buffer exceeds the max file size, but hasn't been
            // flushed yet (i.e. we got a single log entry > max_file_size)
            if buf_bytes > max_file_size && file.bytes_in_file().await? != 0 {
                file = LogFile::create(&parent_path).await?;
            }
            file.write_entries(buf).await?;
        }

        inner.current_file = Some(file);

        self.cleanup_logs(inner).await?;
        Ok(())
    }

    async fn read_most_recent_timestamp(&self) -> Result<Option<Timestamp>> {
        let inner = self.inner.read().await;
        let target_dir = inner.target_dir.as_ref().ok_or(anyhow!("stream not setup"))?;

        let files = LogFile::sort_directory(target_dir).await?;
        for file in files.iter().rev() {
            let entry = file
                .iter_entries()
                .await?
                .filter_map(|l| l.ok())
                .filter_map(|l| match l.data {
                    LogData::TargetLog(data) => Some(data.metadata.timestamp),
                    _ => None,
                })
                .last()
                .await;
            if entry.is_some() {
                return Ok(entry);
            }
        }
        Ok(None)
    }
}

impl DiagnosticsStreamer {
    // This should only be called by tests.
    pub(crate) async fn setup_stream_with_config(
        &self,
        target_nodename: String,
        target_boot_time_nanos: u64,
        cache_directory: PathBuf,
        max_file_size_bytes: usize,
        max_session_size_bytes: usize,
    ) -> Result<()> {
        // The ticks=>time conversion isn't accurate enough to use units smaller than milliseconds here.
        let t = target_boot_time_nanos / 1_000_000;

        let mut output_path: PathBuf = cache_directory.clone();
        output_path.push(target_nodename);
        output_path.push(t.to_string());

        create_dir_all(output_path.to_owned()).await.or_else(|e| match e.kind() {
            ErrorKind::AlreadyExists => Ok(()),
            _ => Err(e),
        })?;

        let mut inner = self.inner.write().await;
        inner.target_dir.replace(output_path);
        inner.max_file_size_bytes = max_file_size_bytes;
        inner.max_session_size_bytes = max_session_size_bytes;
        Ok(())
    }

    async fn cleanup_logs(
        &self,
        inner: RwLockWriteGuard<'_, DiagnosticsStreamerInner>,
    ) -> Result<()> {
        let parent_path = inner.target_dir.as_ref().context("no stream setup")?.clone();
        let mut entries = LogFile::sort_directory(&parent_path).await?;

        // We approximate the need to garbage collect by multiplying the number of files by
        // the max file size, *excluding* the most recent file.
        if entries.len() > 1
            && (entries.len() - 1) * inner.max_file_size_bytes > inner.max_session_size_bytes
        {
            let to_remove = entries.first_mut().unwrap();
            log::info!("logger: garbage collecting log file: {:?}", to_remove);
            to_remove.remove().await?;
        }

        Ok(())
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        async_std::fs::read_to_string,
        diagnostics_data::{LogsField, Severity},
        diagnostics_hierarchy::{DiagnosticsHierarchy, Property},
        std::collections::HashMap,
        tempfile::{tempdir, TempDir},
    };

    const FAKE_DIR_NAME: &str = "fake_logs";
    const SMALL_MAX_LOG_SIZE: usize = 10;
    const LARGE_MAX_LOG_SIZE: usize = 1_000_000;
    const SMALL_MAX_SESSION_SIZE: usize = 11;
    const LARGE_MAX_SESSION_SIZE: usize = 1_000_001;
    const NODENAME: &str = "my-cool-node";
    const BOOT_TIME_NANOS: u64 = 123456789000000000;
    const BOOT_TIME_MILLIS: u64 = 123456789000;
    const TIMESTAMP: u64 = 987654321;

    async fn collect_logs(path: PathBuf) -> Result<HashMap<String, String>> {
        let mut result = HashMap::new();
        let mut ent_stream = read_dir(path).await?;
        while let Some(f) = ent_stream.next().await {
            let f = f?;
            result.insert(
                String::from(f.file_name().to_string_lossy()),
                read_to_string(String::from(f.path().to_str().unwrap())).await?,
            );
        }

        Ok(result)
    }

    async fn collect_default_logs(dir_path: &PathBuf) -> Result<HashMap<String, String>> {
        let mut root = dir_path.clone();
        root.push(FAKE_DIR_NAME);
        root.push(NODENAME);
        root.push(BOOT_TIME_MILLIS.to_string());
        collect_logs(root).await
    }

    fn make_target_log(timestamp: u64, msg: String) -> LogsData {
        let hierarchy =
            DiagnosticsHierarchy::new("root", vec![Property::String(LogsField::Msg, msg)], vec![]);
        LogsData::for_logs(
            String::from("test/moniker"),
            Some(hierarchy),
            timestamp,
            String::from("fake-url"),
            Severity::Error,
            1,
            vec![],
        )
    }

    fn make_malformed_log(ts: u64) -> LogEntry {
        LogEntry {
            data: LogData::MalformedTargetLog("fake log data".to_string()),
            version: 1,
            timestamp: ts.into(),
        }
    }

    fn make_valid_log(ts: u64, msg: String) -> LogEntry {
        LogEntry {
            data: LogData::TargetLog(make_target_log(ts.into(), msg)),
            version: 1,
            timestamp: ts.into(),
        }
    }

    async fn setup_default_streamer_with_temp(
        temp_parent: &TempDir,
        max_log_size: usize,
        max_session_size: usize,
    ) -> Result<DiagnosticsStreamer> {
        let mut root: PathBuf = temp_parent.path().to_path_buf().into();
        root.push(FAKE_DIR_NAME.to_string());

        let streamer = DiagnosticsStreamer::default();
        streamer
            .setup_stream_with_config(
                NODENAME.to_string(),
                BOOT_TIME_NANOS,
                root.clone(),
                max_log_size,
                max_session_size,
            )
            .await?;

        Ok(streamer)
    }

    async fn setup_default_streamer(
        max_log_size: usize,
        max_session_size: usize,
    ) -> Result<(TempDir, DiagnosticsStreamer)> {
        let temp_parent = tempdir()?;
        let streamer =
            setup_default_streamer_with_temp(&temp_parent, max_log_size, max_session_size).await?;

        Ok((temp_parent, streamer))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_single_log_exceeds_max_size() -> Result<()> {
        let (temp, streamer) =
            setup_default_streamer(SMALL_MAX_LOG_SIZE, LARGE_MAX_SESSION_SIZE).await?;
        let root: PathBuf = temp.path().to_path_buf().into();
        let log = make_malformed_log(TIMESTAMP);
        streamer.append_logs(vec![log.clone()]).await?;

        let results = collect_default_logs(&root).await.unwrap();
        assert_eq!(results.len(), 1, "{:?}", results);
        assert_eq!(
            serde_json::from_str::<LogEntry>(results.values().next().unwrap()).unwrap(),
            log
        );

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_two_logs_exceeds_max_size() -> Result<()> {
        let (temp, streamer) =
            setup_default_streamer(SMALL_MAX_LOG_SIZE, LARGE_MAX_SESSION_SIZE).await?;
        let root: PathBuf = temp.path().to_path_buf().into();
        let log = make_malformed_log(TIMESTAMP);
        let log2 = make_malformed_log(TIMESTAMP + 1);
        streamer.append_logs(vec![log.clone(), log2.clone()]).await?;

        let results = collect_default_logs(&root).await.unwrap();
        assert_eq!(results.len(), 2, "{:?}", results);
        let mut values = results.values().collect::<Vec<_>>();
        values.sort();
        assert_eq!(serde_json::from_str::<LogEntry>(values.get(0).unwrap()).unwrap(), log);
        assert_eq!(serde_json::from_str::<LogEntry>(values.get(1).unwrap()).unwrap(), log2);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_two_logs_with_two_calls_exceeds_max_size() -> Result<()> {
        let (temp, streamer) =
            setup_default_streamer(SMALL_MAX_LOG_SIZE, SMALL_MAX_SESSION_SIZE).await?;
        let root: PathBuf = temp.path().to_path_buf().into();
        let log = make_malformed_log(TIMESTAMP);
        let log2 = make_malformed_log(TIMESTAMP + 1);
        streamer.append_logs(vec![log.clone()]).await?;
        streamer.append_logs(vec![log2.clone()]).await?;

        let results = collect_default_logs(&root).await.unwrap();
        assert_eq!(results.len(), 2, "{:?}", results);
        let mut values = results.values().collect::<Vec<_>>();
        values.sort();
        assert_eq!(serde_json::from_str::<LogEntry>(values.get(0).unwrap()).unwrap(), log);
        assert_eq!(serde_json::from_str::<LogEntry>(values.get(1).unwrap()).unwrap(), log2);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_garbage_collection_doesnt_count_latest_file() -> Result<()> {
        let (temp, streamer) =
            setup_default_streamer(SMALL_MAX_LOG_SIZE, SMALL_MAX_LOG_SIZE).await?;
        let root: PathBuf = temp.path().to_path_buf().into();
        let log = make_malformed_log(TIMESTAMP);
        let log2 = make_malformed_log(TIMESTAMP + 1);
        streamer.append_logs(vec![log.clone()]).await?;
        streamer.append_logs(vec![log2.clone()]).await?;

        let results = collect_default_logs(&root).await.unwrap();
        assert_eq!(results.len(), 2, "{:?}", results);
        let mut values = results.values().collect::<Vec<_>>();
        values.sort();
        assert_eq!(serde_json::from_str::<LogEntry>(values.get(0).unwrap()).unwrap(), log);
        assert_eq!(serde_json::from_str::<LogEntry>(values.get(1).unwrap()).unwrap(), log2);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_two_logs_with_two_calls_exceeds_max_session_size() -> Result<()> {
        let (temp, streamer) =
            setup_default_streamer(SMALL_MAX_LOG_SIZE, SMALL_MAX_LOG_SIZE).await?;
        let root: PathBuf = temp.path().to_path_buf().into();
        let log = make_malformed_log(TIMESTAMP);
        let log2 = make_malformed_log(TIMESTAMP + 1);
        let log3 = make_malformed_log(TIMESTAMP + 2);
        streamer.append_logs(vec![log.clone()]).await?;
        streamer.append_logs(vec![log2.clone()]).await?;
        // This call should delete the first log file.
        streamer.append_logs(vec![log3.clone()]).await?;

        let results = collect_default_logs(&root).await.unwrap();
        assert_eq!(results.len(), 2, "{:?}", results);
        let mut values = results.values().collect::<Vec<_>>();
        values.sort();
        assert_eq!(serde_json::from_str::<LogEntry>(values.get(0).unwrap()).unwrap(), log2);
        assert_eq!(serde_json::from_str::<LogEntry>(values.get(1).unwrap()).unwrap(), log3);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_new_streamer_appends_to_existing_file() -> Result<()> {
        let temp = tempdir()?;
        let root: PathBuf = temp.path().to_path_buf().into();
        let log = make_malformed_log(TIMESTAMP);
        let log2 = make_malformed_log(TIMESTAMP + 1);
        {
            let streamer =
                setup_default_streamer_with_temp(&temp, LARGE_MAX_LOG_SIZE, LARGE_MAX_SESSION_SIZE)
                    .await
                    .unwrap();
            streamer.append_logs(vec![log.clone()]).await.unwrap();
        }

        {
            let streamer =
                setup_default_streamer_with_temp(&temp, LARGE_MAX_LOG_SIZE, LARGE_MAX_SESSION_SIZE)
                    .await
                    .unwrap();
            streamer.append_logs(vec![log2.clone()]).await.unwrap();
        }

        let results = collect_default_logs(&root).await.unwrap();
        assert_eq!(results.len(), 1, "{:?}", results);
        let value = results.values().next().unwrap();

        let results = value.split("\n").collect::<Vec<_>>();
        assert_eq!(results.len(), 3, "{:?}", results);

        assert_eq!(serde_json::from_str::<LogEntry>(results.get(0).unwrap()).unwrap(), log);
        assert_eq!(serde_json::from_str::<LogEntry>(results.get(1).unwrap()).unwrap(), log2);
        assert!(results.get(2).unwrap().is_empty());

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_setup_call_errors() -> Result<()> {
        let streamer = DiagnosticsStreamer::default();
        let log = make_malformed_log(TIMESTAMP);
        assert!(streamer.append_logs(vec![log]).await.is_err());

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_simple_read() -> Result<()> {
        let (_temp, streamer) =
            setup_default_streamer(SMALL_MAX_LOG_SIZE, LARGE_MAX_SESSION_SIZE).await?;

        let early_log = make_valid_log(TIMESTAMP - 1, String::default());
        let log = make_valid_log(TIMESTAMP, String::default());

        streamer.append_logs(vec![early_log, log]).await?;

        assert_eq!(streamer.read_most_recent_timestamp().await?.unwrap(), TIMESTAMP.into());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_read_timestamp_returns_none_if_no_logs() -> Result<()> {
        let (_temp, streamer) =
            setup_default_streamer(SMALL_MAX_LOG_SIZE, LARGE_MAX_SESSION_SIZE).await?;

        assert!(streamer.read_most_recent_timestamp().await?.is_none());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_read_timestamp_returns_none_if_logs_malformed() -> Result<()> {
        let (_temp, streamer) =
            setup_default_streamer(SMALL_MAX_LOG_SIZE, LARGE_MAX_SESSION_SIZE).await?;

        let log = make_malformed_log(TIMESTAMP);
        let log2 = make_malformed_log(TIMESTAMP + 1);
        streamer.append_logs(vec![log.clone(), log2.clone()]).await?;

        assert!(streamer.read_most_recent_timestamp().await?.is_none());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_read_timestamp_returns_valid_log() -> Result<()> {
        let (_temp, streamer) =
            setup_default_streamer(SMALL_MAX_LOG_SIZE, LARGE_MAX_SESSION_SIZE).await?;

        let log = make_valid_log(TIMESTAMP, String::default());
        let log2 = make_malformed_log(TIMESTAMP + 1);
        streamer.append_logs(vec![log.clone(), log2.clone()]).await?;

        assert_eq!(streamer.read_most_recent_timestamp().await?.unwrap(), TIMESTAMP.into());
        Ok(())
    }
}
