// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_logger::{
    LogFilterOptions, LogListenerMarker, LogMessage, LogRequest, LogRequestStream, LogSinkRequest,
    LogSinkRequestStream,
};
use fuchsia_async as fasync;
use fuchsia_inspect as inspect;
use futures::{StreamExt, TryFutureExt, TryStreamExt};
use parking_lot::Mutex;
use std::sync::Arc;

mod buffer;
mod klogger;
mod listener;
mod logger;
mod stats;

use listener::Listener;

/// Store 4 MB of log messages and delete on FIFO basis.
const OLD_MSGS_BUF_SIZE: usize = 4 * 1024 * 1024;

/// The `LogManager` is responsible for brokering all logging in the archivist.
#[derive(Clone)]
pub struct LogManager {
    inner: Arc<Mutex<ManagerInner>>,
}

struct ManagerInner {
    listeners: Vec<Listener>,
    log_msg_buffer: buffer::MemoryBoundedBuffer<LogMessage>,
    stats: stats::LogManagerStats,
}

impl LogManager {
    pub fn new(node: inspect::Node) -> Self {
        Self {
            inner: Arc::new(Mutex::new(ManagerInner {
                listeners: Vec::new(),
                log_msg_buffer: buffer::MemoryBoundedBuffer::new(
                    OLD_MSGS_BUF_SIZE,
                    node.create_child("buffer_stats"),
                ),
                stats: stats::LogManagerStats::new(node),
            })),
        }
    }

    /// Spawn a task to read from the kernel's debuglog.
    pub fn spawn_klog_drainer(&self) -> Result<(), Error> {
        let mut kernel_logger =
            klogger::KernelLogger::create().context("creating kernel debuglog bridge")?;

        for (log_msg, size) in
            kernel_logger.existing_logs().context("reading from kernel log iterator")?
        {
            self.ingest_message(log_msg, size, stats::LogSource::Kernel);
        }

        let manager = self.clone();
        fasync::spawn(
            kernel_logger
                .listen()
                .map_ok(move |(log_msg, size)| {
                    manager.ingest_message(log_msg, size, stats::LogSource::Kernel);
                })
                .try_collect::<()>()
                .unwrap_or_else(|e| eprintln!("failed to read kernel logs: {:?}", e)),
        );
        Ok(())
    }

    /// Spawn a task to handle requests from components with logs.
    pub fn spawn_log_sink_handler(&self, stream: LogSinkRequestStream) {
        let handler = self.clone();
        fasync::spawn(async move {
            if let Err(e) = handler.handle_log_sink_requests(stream).await {
                eprintln!("logsink stream errored: {:?}", e);
            }
        })
    }

    /// Handle incoming LogSink requests, currently only to receive sockets that will contain logs.
    async fn handle_log_sink_requests(self, mut stream: LogSinkRequestStream) -> Result<(), Error> {
        while let Some(LogSinkRequest::Connect { socket, control_handle }) =
            stream.try_next().await?
        {
            let log_stream = match logger::LoggerStream::new(socket)
                .context("creating log stream from socket")
            {
                Ok(s) => s,
                Err(e) => {
                    control_handle.shutdown();
                    return Err(e);
                }
            };

            fasync::spawn(self.clone().drain_messages(log_stream));
        }
        Ok(())
    }

    /// Drain a `LoggerStream` which wraps a socket from a component generating logs.
    async fn drain_messages(self, mut log_stream: logger::LoggerStream) {
        while let Some(next) = log_stream.next().await {
            match next {
                Ok((log_msg, size)) => {
                    self.ingest_message(log_msg, size, stats::LogSource::LogSink)
                }
                Err(e) => {
                    eprintln!("log stream errored: {:?}", e);
                    return;
                }
            }
        }
    }

    /// Spawn a task to handle requests from components reading the shared log.
    pub fn spawn_log_handler(&self, stream: LogRequestStream) {
        let state = Arc::new(self.clone());
        fasync::spawn(
            stream
                .map_ok(move |req| {
                    let state = state.clone();
                    match req {
                        LogRequest::Listen { log_listener, options, .. } => {
                            state.handle_listener(log_listener, options, false)
                        }
                        LogRequest::DumpLogs { log_listener, options, .. } => {
                            state.handle_listener(log_listener, options, true)
                        }
                    }
                })
                .try_collect::<()>()
                .unwrap_or_else(|e| eprintln!("Log manager failed: {:?}", e)),
        )
    }

    /// Handle a new listener, sending it all cached messages and either calling `Done` if
    /// `dump_logs` is true or adding it to the pool of ongoing listeners if not.
    fn handle_listener(
        &self,
        log_listener: ClientEnd<LogListenerMarker>,
        options: Option<Box<LogFilterOptions>>,
        dump_logs: bool,
    ) {
        let mut listener = match listener::Listener::new(log_listener, options) {
            Ok(w) => w,
            Err(e) => {
                eprintln!("couldn't init listener: {:?}", e);
                return;
            }
        };

        let mut shared_members = self.inner.lock();
        listener.backfill(shared_members.log_msg_buffer.iter());
        if !listener.is_healthy() {
            eprintln!("listener dropped before we finished");
            return;
        }

        if dump_logs {
            listener.done();
        } else {
            shared_members.listeners.push(listener);
        }
    }

    /// Ingest an individual log message.
    fn ingest_message(&self, mut log_msg: LogMessage, size: usize, source: stats::LogSource) {
        let mut inner = self.inner.lock();

        for listener in &mut inner.listeners {
            listener.send_log(&mut log_msg);
        }
        inner.listeners.retain(Listener::is_healthy);
        inner.stats.record_log(&log_msg, source);
        inner.log_msg_buffer.push(log_msg, size);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::logs::logger::fx_log_packet_t,
        fidl_fuchsia_logger::{
            LogFilterOptions, LogLevelFilter, LogMarker, LogProxy, LogSinkMarker, LogSinkProxy,
        },
        fuchsia_inspect::assert_inspect_tree,
        fuchsia_zircon as zx,
        std::collections::HashSet,
        validating_log_listener::{validate_log_dump, validate_log_stream},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_log_manager_simple() {
        TestHarness::new().manager_test(false).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_log_manager_dump() {
        TestHarness::new().manager_test(true).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_filter_by_pid() {
        let p = setup_default_packet();
        let mut p2 = p.clone();
        p2.metadata.pid = 0;
        let lm = LogMessage {
            pid: p.metadata.pid,
            tid: p.metadata.tid,
            time: p.metadata.time,
            dropped_logs: p.metadata.dropped_logs,
            severity: p.metadata.severity,
            msg: String::from("BBBBB"),
            tags: vec![String::from("AAAAA")],
        };
        let options = LogFilterOptions {
            filter_by_pid: true,
            pid: 1,
            filter_by_tid: false,
            tid: 0,
            min_severity: LogLevelFilter::None,
            verbosity: 0,
            tags: vec![],
        };

        let log_stats_tree = TestHarness::new()
            .filter_test(vec![lm].into_iter().collect(), vec![p, p2], Some(options))
            .await;

        assert_inspect_tree!(log_stats_tree,
        root: {
            log_stats: {
                total_logs: 2u64,
                kernel_logs: 0u64,
                logsink_logs: 2u64,
                verbose_logs: 0u64,
                info_logs: 0u64,
                warning_logs: 2u64,
                error_logs: 0u64,
                fatal_logs: 0u64,
                buffer_stats: {
                    rolled_out_entries: 0u64,
                }
            },
        });
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_filter_by_tid() {
        let mut p = setup_default_packet();
        p.metadata.pid = 0;
        let mut p2 = p.clone();
        p2.metadata.tid = 0;
        let lm = LogMessage {
            pid: p.metadata.pid,
            tid: p.metadata.tid,
            time: p.metadata.time,
            dropped_logs: p.metadata.dropped_logs,
            severity: p.metadata.severity,
            msg: String::from("BBBBB"),
            tags: vec![String::from("AAAAA")],
        };
        let options = LogFilterOptions {
            filter_by_pid: false,
            pid: 1,
            filter_by_tid: true,
            tid: 1,
            min_severity: LogLevelFilter::None,
            verbosity: 0,
            tags: vec![],
        };

        TestHarness::new()
            .filter_test(vec![lm].into_iter().collect(), vec![p, p2], Some(options))
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_filter_by_min_severity() {
        let p = setup_default_packet();
        let mut p2 = p.clone();
        p2.metadata.pid = 0;
        p2.metadata.tid = 0;
        p2.metadata.severity = LogLevelFilter::Error.into_primitive().into();
        let mut p3 = p.clone();
        p3.metadata.severity = LogLevelFilter::Info.into_primitive().into();
        let mut p4 = p.clone();
        p4.metadata.severity = -22;
        let mut p5 = p.clone();
        p5.metadata.severity = LogLevelFilter::Fatal.into_primitive().into();
        let lm = LogMessage {
            pid: p2.metadata.pid,
            tid: p2.metadata.tid,
            time: p2.metadata.time,
            dropped_logs: p2.metadata.dropped_logs,
            severity: p2.metadata.severity,
            msg: String::from("BBBBB"),
            tags: vec![String::from("AAAAA")],
        };
        let options = LogFilterOptions {
            filter_by_pid: false,
            pid: 1,
            filter_by_tid: false,
            tid: 1,
            min_severity: LogLevelFilter::Error,
            verbosity: 0,
            tags: vec![],
        };

        let log_stats_tree = TestHarness::new()
            .filter_test(vec![lm].into_iter().collect(), vec![p, p2, p3, p4, p5], Some(options))
            .await;
        assert_inspect_tree!(log_stats_tree,
        root: {
            log_stats: contains {
                total_logs: 5u64,
                kernel_logs: 0u64,
                logsink_logs: 5u64,
                verbose_logs: 1u64,
                info_logs: 1u64,
                warning_logs: 1u64,
                error_logs: 1u64,
                fatal_logs: 1u64,
                buffer_stats: {
                    rolled_out_entries: 0u64,
                }
            },
        });
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_filter_by_combination() {
        let mut p = setup_default_packet();
        p.metadata.pid = 0;
        p.metadata.tid = 0;
        let mut p2 = p.clone();
        p2.metadata.severity = LogLevelFilter::Error.into_primitive().into();
        let mut p3 = p.clone();
        p3.metadata.pid = 1;
        let lm = LogMessage {
            pid: p2.metadata.pid,
            tid: p2.metadata.tid,
            time: p2.metadata.time,
            dropped_logs: p2.metadata.dropped_logs,
            severity: p2.metadata.severity,
            msg: String::from("BBBBB"),
            tags: vec![String::from("AAAAA")],
        };
        let options = LogFilterOptions {
            filter_by_pid: true,
            pid: 0,
            filter_by_tid: false,
            tid: 1,
            min_severity: LogLevelFilter::Error,
            verbosity: 0,
            tags: vec![],
        };

        TestHarness::new()
            .filter_test(vec![lm].into_iter().collect(), vec![p, p2, p3], Some(options))
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_filter_by_tags() {
        let mut p = setup_default_packet();
        let mut p2 = p.clone();
        // p tags - "DDDDD"
        memset(&mut p.data[..], 1, 68, 5);

        p2.metadata.pid = 0;
        p2.metadata.tid = 0;
        p2.data[6] = 5;
        // p2 tag - "AAAAA", "BBBBB"
        // p2 msg - "CCCCC"
        memset(&mut p2.data[..], 13, 67, 5);

        let lm1 = LogMessage {
            pid: p.metadata.pid,
            tid: p.metadata.tid,
            time: p.metadata.time,
            dropped_logs: p.metadata.dropped_logs,
            severity: p.metadata.severity,
            msg: String::from("BBBBB"),
            tags: vec![String::from("DDDDD")],
        };
        let lm2 = LogMessage {
            pid: p2.metadata.pid,
            tid: p2.metadata.tid,
            time: p2.metadata.time,
            dropped_logs: p2.metadata.dropped_logs,
            severity: p2.metadata.severity,
            msg: String::from("CCCCC"),
            tags: vec![String::from("AAAAA"), String::from("BBBBB")],
        };
        let options = LogFilterOptions {
            filter_by_pid: false,
            pid: 1,
            filter_by_tid: false,
            tid: 1,
            min_severity: LogLevelFilter::None,
            verbosity: 0,
            tags: vec![String::from("BBBBB"), String::from("DDDDD")],
        };

        TestHarness::new()
            .filter_test(vec![lm1, lm2].into_iter().collect(), vec![p, p2], Some(options))
            .await;
    }

    struct TestHarness {
        log_proxy: LogProxy,
        sin: zx::Socket,
        inspector: inspect::Inspector,
        _log_sink_proxy: LogSinkProxy,
    }

    impl TestHarness {
        fn new() -> Self {
            let inspector = inspect::Inspector::new();
            let (sin, sout) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
            let node = inspector.root().create_child("log_stats");
            let buffer_node = node.create_child("buffer_stats");
            let inner = Arc::new(Mutex::new(ManagerInner {
                listeners: Vec::new(),
                log_msg_buffer: buffer::MemoryBoundedBuffer::new(OLD_MSGS_BUF_SIZE, buffer_node),
                stats: stats::LogManagerStats::new(node),
            }));

            let lm = LogManager { inner };

            let (log_proxy, log_stream) =
                fidl::endpoints::create_proxy_and_stream::<LogMarker>().unwrap();
            lm.spawn_log_handler(log_stream);

            let (log_sink_proxy, log_sink_stream) =
                fidl::endpoints::create_proxy_and_stream::<LogSinkMarker>().unwrap();
            lm.spawn_log_sink_handler(log_sink_stream);

            log_sink_proxy.connect(sout).expect("unable to connect out socket to log sink");

            Self { log_proxy, _log_sink_proxy: log_sink_proxy, sin, inspector }
        }

        /// Run a filter test, returning the Inspector to check Inspect output.
        async fn filter_test(
            self,
            expected: HashSet<LogMessage>,
            packets: Vec<fx_log_packet_t>,
            filter_options: Option<LogFilterOptions>,
        ) -> inspect::Inspector {
            for mut p in packets {
                self.sin.write(to_u8_slice(&mut p)).unwrap();
            }

            validate_log_stream(expected, self.log_proxy, filter_options).await;
            self.inspector
        }

        async fn manager_test(self, test_dump_logs: bool) {
            let mut p = setup_default_packet();
            self.sin.write(to_u8_slice(&mut p)).unwrap();

            p.metadata.severity = LogLevelFilter::Info.into_primitive().into();
            self.sin.write(to_u8_slice(&mut p)).unwrap();

            let mut lm1 = LogMessage {
                time: p.metadata.time,
                pid: p.metadata.pid,
                tid: p.metadata.tid,
                dropped_logs: p.metadata.dropped_logs,
                severity: p.metadata.severity,
                msg: String::from("BBBBB"),
                tags: vec![String::from("AAAAA")],
            };
            let lm2 = copy_log_message(&lm1);
            lm1.severity = LogLevelFilter::Warn.into_primitive().into();
            let mut lm3 = copy_log_message(&lm2);
            lm3.pid = 2;

            p.metadata.pid = 2;
            self.sin.write(to_u8_slice(&mut p)).unwrap();

            if test_dump_logs {
                validate_log_dump(vec![lm1, lm2, lm3], self.log_proxy, None).await;
            } else {
                validate_log_stream(vec![lm1, lm2, lm3], self.log_proxy, None).await;
            }
        }
    }

    fn setup_default_packet() -> fx_log_packet_t {
        let mut p: fx_log_packet_t = Default::default();
        p.metadata.pid = 1;
        p.metadata.tid = 1;
        p.metadata.severity = LogLevelFilter::Warn.into_primitive().into();
        p.metadata.dropped_logs = 2;
        p.data[0] = 5;
        memset(&mut p.data[..], 1, 65, 5);
        memset(&mut p.data[..], 7, 66, 5);
        return p;
    }

    fn copy_log_message(log_message: &LogMessage) -> LogMessage {
        LogMessage {
            pid: log_message.pid,
            tid: log_message.tid,
            time: log_message.time,
            severity: log_message.severity,
            dropped_logs: log_message.dropped_logs,
            tags: log_message.tags.clone(),
            msg: log_message.msg.clone(),
        }
    }

    /// Function to convert fx_log_packet_t to &[u8].
    /// This function is safe as it works on `fx_log_packet_t` which
    /// doesn't have any uninitialized padding bits.
    fn to_u8_slice(p: &fx_log_packet_t) -> &[u8] {
        // This code just converts to &[u8] so no need to explicity drop it as memory
        // location would be freed as soon as p is dropped.
        unsafe {
            ::std::slice::from_raw_parts(
                (p as *const fx_log_packet_t) as *const u8,
                ::std::mem::size_of::<fx_log_packet_t>(),
            )
        }
    }

    fn memset<T: Copy>(x: &mut [T], offset: usize, value: T, size: usize) {
        x[offset..(offset + size)].iter_mut().for_each(|x| *x = value);
    }
}
