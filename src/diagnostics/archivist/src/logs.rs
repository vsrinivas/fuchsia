// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_logger::{
    LogFilterOptions, LogListenerSafeMarker, LogRequest, LogRequestStream, LogSinkRequest,
    LogSinkRequestStream,
};
use fidl_fuchsia_sys_internal::{
    LogConnection, LogConnectionListenerRequest, LogConnectorProxy, SourceIdentity,
};
use fuchsia_async as fasync;
use fuchsia_inspect as inspect;
use fuchsia_zircon as zx;
use futures::{
    channel::mpsc, future, lock::Mutex, sink::SinkExt, FutureExt, StreamExt, TryStreamExt,
};
use log::{error, warn};
use std::sync::Arc;

mod buffer;
mod debuglog;
mod error;
mod listener;
pub mod message;
mod socket;
mod stats;

pub use debuglog::{convert_debuglog_to_log_message, KernelDebugLog};
use listener::{pool::Pool, pretend_scary_listener_is_safe, Listener};
use message::Message;
use socket::LogMessageSocket;
use stats::LogSource;

/// Store 4 MB of log messages and delete on FIFO basis.
const OLD_MSGS_BUF_SIZE: usize = 4 * 1024 * 1024;

/// The `LogManager` is responsible for brokering all logging in the archivist.
#[derive(Clone)]
pub struct LogManager {
    inner: Arc<Mutex<ManagerInner>>,
}

struct ManagerInner {
    listeners: Pool,
    log_msg_buffer: buffer::MemoryBoundedBuffer<Message>,
    stats: stats::LogManagerStats,
}

impl LogManager {
    pub fn new(node: inspect::Node) -> Self {
        Self {
            inner: Arc::new(Mutex::new(ManagerInner {
                listeners: Pool::default(),
                log_msg_buffer: buffer::MemoryBoundedBuffer::new(
                    OLD_MSGS_BUF_SIZE,
                    node.create_child("buffer_stats"),
                ),
                stats: stats::LogManagerStats::new(node),
            })),
        }
    }

    /// Spawn a task to read from the kernel's debuglog. The returned future completes once existing
    /// messages have been ingested.
    pub async fn spawn_debuglog_drainer<K>(&self, klog_reader: K) -> Result<(), Error>
    where
        K: debuglog::DebugLog + Send + Sync + 'static,
    {
        let mut kernel_logger = debuglog::DebugLogBridge::create(klog_reader);
        for log_msg in
            kernel_logger.existing_logs().await.context("reading from kernel log iterator")?
        {
            self.ingest_message(log_msg, LogSource::Kernel).await;
        }

        let manager = self.clone();
        fasync::spawn(async move {
            let drain_result = kernel_logger
                .listen()
                .try_for_each(|log_msg| manager.ingest_message(log_msg, LogSource::Kernel).map(Ok));

            if let Err(e) = drain_result.await {
                error!(
                    "important logs may be missing from system log.\
                           failed to drain kernel log: {:?}",
                    e
                );
            }
        });

        Ok(())
    }

    /// Spawn a log sink for messages sent by the archivist itself.
    pub fn spawn_internal_sink(&self, socket: zx::Socket, name: &str) -> Result<(), Error> {
        // TODO(50105): Figure out how to properly populate SourceIdentity
        let mut source = SourceIdentity::empty();
        source.component_name = Some(name.to_owned());
        let log_stream = LogMessageSocket::new(socket, source)?;
        let manager = self.clone();
        fasync::spawn(async move {
            manager.drain_messages(log_stream).await;
            unreachable!();
        });
        Ok(())
    }

    /// Spawn a task which attempts to act as `LogConnectionListener` for its parent realm, eventually
    /// passing `LogSink` connections into the manager.
    /// TODO(49764): Make this run on main future
    pub fn spawn_log_consumer(&self, connector: LogConnectorProxy) {
        let handler = self.clone();
        fasync::spawn(async move {
            match connector.take_log_connection_listener().await {
                Ok(Some(listener)) => {
                    let mut connections =
                        listener.into_stream().expect("getting request stream from server end");
                    while let Ok(Some(connection)) = connections.try_next().await {
                        match connection {
                            LogConnectionListenerRequest::OnNewConnection {
                                connection: LogConnection { log_request, source_identity },
                                control_handle: _,
                            } => {
                                let fut = handler.clone().process_log_sink(
                                    log_request
                                        .into_stream()
                                        .expect("getting LogSinkRequestStream from serverend"),
                                    source_identity,
                                );

                                fasync::spawn(fut);
                            }
                        };
                    }
                }
                Ok(None) => {
                    warn!("local realm already gave out LogConnectionListener, skipping logs")
                }
                Err(why) => {
                    error!("error retrieving LogConnectionListener from LogConnector: {:?}", why)
                }
            }
        });
    }

    /// Process LogSink protocol on `stream`. The future returned by this
    /// function will not complete before all messages on this connection are
    /// processed.
    pub async fn process_log_sink(self, mut stream: LogSinkRequestStream, source: SourceIdentity) {
        if source.component_name.is_none() {
            self.inner.lock().await.stats.record_unattributed();
        }
        let (mut log_stream_sender, recv) = mpsc::channel(0);
        let log_sink_fut = async move {
            while let Some(LogSinkRequest::Connect { socket, control_handle }) =
                stream.try_next().await?
            {
                let source = SourceIdentity {
                    component_name: source.component_name.clone(),
                    component_url: source.component_url.clone(),
                    instance_id: source.instance_id.clone(),
                    realm_path: source.realm_path.clone(),
                };
                let log_stream = match LogMessageSocket::new(socket, source)
                    .context("creating log stream from socket")
                {
                    Ok(s) => s,
                    Err(e) => {
                        control_handle.shutdown();
                        return Err(e);
                    }
                };
                log_stream_sender.send(log_stream).await?;
            }
            Ok(())
        };

        let log_sink_fut = async move {
            if let Err(e) = log_sink_fut.await {
                eprintln!("logsink stream errored: {:?}", e);
            }
        };

        // move it into async block else two futures are no compatiable to join.
        let drain_messages_fut = async move {
            recv.for_each_concurrent(None, |log_stream| async {
                self.clone().drain_messages(log_stream).await;
            })
            .await;
        };

        // Join future processing LogSink and future processing sockets so as to
        // drain all the messages.
        future::join(log_sink_fut, drain_messages_fut).await;
    }

    /// Drain a `LoggerStream` which wraps a socket from a component generating logs.
    async fn drain_messages(self, mut log_stream: LogMessageSocket) {
        while let Some(next) = log_stream.next().await {
            match next {
                Ok(log_msg) => {
                    self.ingest_message(log_msg, stats::LogSource::LogSink).await;
                }
                Err(e) => {
                    self.inner.lock().await.stats.record_closed_stream();
                    warn!("closing socket from {:?} due to error: {}", log_stream.source(), e);
                    return;
                }
            }
        }
    }

    /// Spawn a task to handle requests from components reading the shared log.
    pub fn spawn_log_handler(&self, stream: LogRequestStream) {
        let handler = self.clone();
        fasync::spawn(async move {
            if let Err(e) = handler.handle_log_requests(stream).await {
                warn!("error handling Log requests: {:?}", e);
            }
        });
    }

    /// Handle requests to `fuchsia.logger.Log`. All request types read the whole backlog from
    /// memory, `DumpLogs(Safe)` stops listening after that though.
    async fn handle_log_requests(self, mut stream: LogRequestStream) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            let (listener, options, dump_logs) = match request {
                LogRequest::ListenSafe { log_listener, options, .. } => {
                    (log_listener, options, false)
                }
                LogRequest::DumpLogsSafe { log_listener, options, .. } => {
                    (log_listener, options, true)
                }

                // TODO(fxb/48758) delete these methods!
                LogRequest::Listen { log_listener, options, .. } => {
                    warn!("Use of fuchsia.logger.Log.Listen. Use ListenSafe.");
                    let listener = pretend_scary_listener_is_safe(log_listener)?;
                    (listener, options, false)
                }
                LogRequest::DumpLogs { log_listener, options, .. } => {
                    warn!("Use of fuchsia.logger.Log.DumpLogs. Use DumpLogsSafe.");
                    let listener = pretend_scary_listener_is_safe(log_listener)?;
                    (listener, options, true)
                }
            };

            self.handle_listener(listener, options, dump_logs).await?;
        }
        Ok(())
    }

    /// Handle a new listener, sending it all cached messages and either calling `Done` if
    /// `dump_logs` is true or adding it to the pool of ongoing listeners if not.
    async fn handle_listener(
        &self,
        log_listener: ClientEnd<LogListenerSafeMarker>,
        options: Option<Box<LogFilterOptions>>,
        dump_logs: bool,
    ) -> Result<(), Error> {
        let mut listener = Listener::new(log_listener, options)?;

        let mut inner = self.inner.lock().await;
        listener.backfill(inner.log_msg_buffer.iter()).await;

        if !listener.is_healthy() {
            warn!("listener dropped before we finished");
            return Ok(());
        }

        if dump_logs {
            listener.done();
        } else {
            inner.listeners.add(listener);
        }
        Ok(())
    }

    /// Ingest an individual log message.
    async fn ingest_message(&self, log_msg: Message, source: stats::LogSource) {
        let mut inner = self.inner.lock().await;

        inner.stats.record_log(&log_msg, source);
        inner.listeners.send(&log_msg).await;
        inner.log_msg_buffer.push(log_msg);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::logs::debuglog::tests::{TestDebugEntry, TestDebugLog},
        crate::logs::message::fx_log_packet_t,
        fidl_fuchsia_logger::{
            LogFilterOptions, LogLevelFilter, LogMarker, LogMessage, LogProxy, LogSinkMarker,
            LogSinkProxy,
        },
        fuchsia_inspect::assert_inspect_tree,
        fuchsia_zircon as zx,
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
    async fn unfiltered_stats() {
        let first_packet = setup_default_packet();
        let first_message = LogMessage {
            pid: first_packet.metadata.pid,
            tid: first_packet.metadata.tid,
            time: first_packet.metadata.time,
            dropped_logs: first_packet.metadata.dropped_logs,
            severity: first_packet.metadata.severity,
            msg: String::from("BBBBB"),
            tags: vec![String::from("AAAAA")],
        };

        let (mut second_packet, mut second_message) = (first_packet.clone(), first_message.clone());
        second_packet.metadata.pid = 0;
        second_message.pid = second_packet.metadata.pid;

        let (mut third_packet, mut third_message) = (second_packet.clone(), second_message.clone());
        third_packet.metadata.severity = LogLevelFilter::Info.into_primitive().into();
        third_message.severity = third_packet.metadata.severity;

        let (fourth_packet, fourth_message) = (third_packet.clone(), third_message.clone());

        let (mut fifth_packet, mut fifth_message) = (fourth_packet.clone(), fourth_message.clone());
        fifth_packet.metadata.severity = LogLevelFilter::Error.into_primitive().into();
        fifth_message.severity = fifth_packet.metadata.severity;

        let log_stats_tree = TestHarness::new()
            .filter_test(
                vec![first_message, second_message, third_message, fourth_message, fifth_message],
                vec![first_packet, second_packet, third_packet, fourth_packet, fifth_packet],
                None,
            )
            .await;

        assert_inspect_tree!(
            log_stats_tree,
            root: {
                log_stats: {
                    total_logs: 5u64,
                    kernel_logs: 0u64,
                    logsink_logs: 5u64,
                    trace_logs: 0u64,
                    debug_logs: 0u64,
                    info_logs: 2u64,
                    warning_logs: 2u64,
                    error_logs: 1u64,
                    fatal_logs: 0u64,
                    closed_streams: 0u64,
                    unattributed_log_sinks: 1u64,
                    buffer_stats: {
                        rolled_out_entries: 0u64,
                    }
                },
            }
        );
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

        TestHarness::new().filter_test(vec![lm], vec![p, p2], Some(options)).await;
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

        TestHarness::new().filter_test(vec![lm], vec![p, p2], Some(options)).await;
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

        TestHarness::new().filter_test(vec![lm], vec![p, p2, p3, p4, p5], Some(options)).await;
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

        TestHarness::new().filter_test(vec![lm], vec![p, p2, p3], Some(options)).await;
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

        TestHarness::new().filter_test(vec![lm1, lm2], vec![p, p2], Some(options)).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_debuglog_drainer() {
        let log1 = TestDebugEntry::new("log1".as_bytes());
        let log2 = TestDebugEntry::new("log2".as_bytes());
        let log3 = TestDebugEntry::new("log3".as_bytes());

        let klog_reader = TestDebugLog::new();
        klog_reader.enqueue_read_entry(&log1);
        klog_reader.enqueue_read_entry(&log2);
        // logs recieved after kernel indicates no logs should be read
        klog_reader.enqueue_read_fail(zx::Status::SHOULD_WAIT);
        klog_reader.enqueue_read_entry(&log3);
        klog_reader.enqueue_read_fail(zx::Status::SHOULD_WAIT);

        let expected_logs = vec![
            LogMessage {
                pid: log1.pid,
                tid: log1.tid,
                time: log1.timestamp,
                dropped_logs: 0,
                severity: fidl_fuchsia_logger::LogLevelFilter::Info as i32,
                msg: String::from("log1"),
                tags: vec![String::from("klog")],
            },
            LogMessage {
                pid: log2.pid,
                tid: log2.tid,
                time: log2.timestamp,
                dropped_logs: 0,
                severity: fidl_fuchsia_logger::LogLevelFilter::Info as i32,
                msg: String::from("log2"),
                tags: vec![String::from("klog")],
            },
            LogMessage {
                pid: log3.pid,
                tid: log3.tid,
                time: log3.timestamp,
                dropped_logs: 0,
                severity: fidl_fuchsia_logger::LogLevelFilter::Info as i32,
                msg: String::from("log3"),
                tags: vec![String::from("klog")],
            },
        ];

        let klog_stats_tree = debuglog_test(expected_logs, klog_reader).await;
        assert_inspect_tree!(
            klog_stats_tree,
            root: {
                log_stats: contains {
                    total_logs: 3u64,
                    kernel_logs: 3u64,
                    logsink_logs: 0u64,
                    trace_logs: 0u64,
                    debug_logs: 0u64,
                    info_logs: 3u64,
                    warning_logs: 0u64,
                    error_logs: 0u64,
                    fatal_logs: 0u64,
                    closed_streams: 0u64,
                    unattributed_log_sinks: 0u64,
                }
            }
        );
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
                listeners: Pool::default(),
                log_msg_buffer: buffer::MemoryBoundedBuffer::new(OLD_MSGS_BUF_SIZE, buffer_node),
                stats: stats::LogManagerStats::new(node),
            }));

            let lm = LogManager { inner };

            let (log_proxy, log_stream) =
                fidl::endpoints::create_proxy_and_stream::<LogMarker>().unwrap();
            lm.spawn_log_handler(log_stream);

            let (log_sink_proxy, log_sink_stream) =
                fidl::endpoints::create_proxy_and_stream::<LogSinkMarker>().unwrap();

            let manager = lm.clone();
            fasync::spawn(async move {
                manager.process_log_sink(log_sink_stream, SourceIdentity::empty()).await;
            });

            log_sink_proxy.connect(sout).expect("unable to connect out socket to log sink");

            Self { log_proxy, _log_sink_proxy: log_sink_proxy, sin, inspector }
        }

        /// Run a filter test, returning the Inspector to check Inspect output.
        async fn filter_test(
            self,
            expected: impl IntoIterator<Item = LogMessage>,
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

    /// Run a test on logs from klog, returning the inspector object.
    async fn debuglog_test(
        expected: impl IntoIterator<Item = LogMessage>,
        debug_log: TestDebugLog,
    ) -> inspect::Inspector {
        let inspector = inspect::Inspector::new();
        let node = inspector.root().create_child("log_stats");
        let buffer_node = node.create_child("buffer_stats");
        let inner = Arc::new(Mutex::new(ManagerInner {
            listeners: Pool::default(),
            log_msg_buffer: buffer::MemoryBoundedBuffer::new(OLD_MSGS_BUF_SIZE, buffer_node),
            stats: stats::LogManagerStats::new(node),
        }));

        let lm = LogManager { inner };
        let (log_proxy, log_stream) =
            fidl::endpoints::create_proxy_and_stream::<LogMarker>().unwrap();
        lm.spawn_log_handler(log_stream);
        lm.spawn_debuglog_drainer(debug_log).await.unwrap();

        validate_log_stream(expected, log_proxy, None).await;
        inspector
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
