// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    async_trait::async_trait,
    chrono::{Local, TimeZone},
    diagnostics_data::Timestamp,
    errors::ffx_error,
    ffx_log_data::{EventType, LogData, LogEntry},
    ffx_log_utils::{run_logging_pipeline, OrderedBatchPipeline},
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_developer_ffx::{
        DaemonDiagnosticsStreamParameters, DiagnosticsProxy, DiagnosticsStreamError, LogSession,
        SessionSpec, StreamMode, TimeBound,
    },
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorError, ArchiveIteratorMarker, ArchiveIteratorProxy, DiagnosticsData,
    },
    fuchsia_async::Timer,
    futures::AsyncReadExt,
    std::{
        iter::Iterator,
        time::{Duration, SystemTime},
    },
    timeout::timeout,
};

type ArchiveIteratorResult = Result<LogEntry, ArchiveIteratorError>;
const PIPELINE_SIZE: usize = 1;
const NO_STREAM_ERROR: &str = "\
The proactive logger isn't connected to this target.

Verify that the target is up with `ffx target list` and retry \
in a few seconds. If the issue persists, run the following command:

$ ffx doctor --restart-daemon";
const NANOS_IN_SECOND: i64 = 1_000_000_000;
const TIMESTAMP_FORMAT: &str = "%Y-%m-%d %H:%M:%S.%3f";
const RETRY_TIMEOUT_MILLIS: u64 = 1000;

fn get_timestamp() -> Result<Timestamp> {
    Ok(Timestamp::from(
        SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .context("system time before Unix epoch")?
            .as_nanos() as i64,
    ))
}

fn format_ffx_event(msg: &str, timestamp: Option<Timestamp>) -> String {
    let ts: i64 = timestamp.unwrap_or_else(|| get_timestamp().unwrap()).into();
    let dt = Local
        .timestamp(ts / NANOS_IN_SECOND, (ts % NANOS_IN_SECOND) as u32)
        .format(TIMESTAMP_FORMAT)
        .to_string();
    format!("[{}][<ffx>]: {}", dt, msg)
}

#[async_trait(?Send)]
pub trait LogFormatter {
    async fn push_log(&mut self, log_entry: ArchiveIteratorResult) -> Result<()>;
    fn set_boot_timestamp(&mut self, boot_ts_nanos: i64);
}

#[derive(Clone, Debug)]
pub struct LogCommandParameters {
    pub target_identifier: String,
    pub session: Option<SessionSpec>,
    pub from_bound: Option<TimeBound>,
    pub to_bound: Option<TimeBound>,
    pub stream_mode: StreamMode,
}

impl Default for LogCommandParameters {
    fn default() -> Self {
        Self {
            target_identifier: String::default(),
            session: Some(SessionSpec::Relative(0)),
            from_bound: None,
            to_bound: None,
            stream_mode: StreamMode::SnapshotAll,
        }
    }
}

async fn setup_diagnostics_stream(
    diagnostics_proxy: &DiagnosticsProxy,
    target_str: &str,
    server: ServerEnd<ArchiveIteratorMarker>,
    stream_mode: StreamMode,
    from_bound: Option<TimeBound>,
    session: Option<SessionSpec>,
) -> Result<Result<LogSession, DiagnosticsStreamError>> {
    let params = DaemonDiagnosticsStreamParameters {
        stream_mode: Some(stream_mode),
        min_timestamp_nanos: from_bound,
        session,
        ..DaemonDiagnosticsStreamParameters::EMPTY
    };
    diagnostics_proxy
        .stream_diagnostics(Some(&target_str), params, server)
        .await
        .context("connecting to daemon")
}

pub async fn exec_log_cmd<W: std::io::Write>(
    params: LogCommandParameters,
    diagnostics_proxy: DiagnosticsProxy,
    log_formatter: &mut impl LogFormatter,
    writer: &mut W,
) -> Result<()> {
    let (mut proxy, server) =
        create_proxy::<ArchiveIteratorMarker>().context("failed to create endpoints")?;

    let session = setup_diagnostics_stream(
        &diagnostics_proxy,
        &params.target_identifier,
        server,
        params.stream_mode,
        params.from_bound.clone(),
        params.session.clone(),
    )
    .await?
    .map_err(|e| match e {
        DiagnosticsStreamError::NoStreamForTarget => anyhow!(ffx_error!("{}", NO_STREAM_ERROR)),
        _ => anyhow!("failure setting up diagnostics stream: {:?}", e),
    })?;

    let session_timestamp_nanos =
        session.session_timestamp_nanos.as_ref().context("missing session timestamp")?;
    log_formatter.set_boot_timestamp(*session_timestamp_nanos as i64);

    let to_bound_monotonic = params.to_bound.as_ref().map(|bound| match bound {
        TimeBound::Monotonic(ts) => Duration::from_nanos(*ts),
        TimeBound::Absolute(ts) => Duration::from_nanos(ts - session_timestamp_nanos),
        _ => panic!("unexpected TimeBound value"),
    });

    let mut requests = OrderedBatchPipeline::new(PIPELINE_SIZE);
    // This variable is set to true iff the most recent log we received was a disconnect event.
    let mut got_disconnect = false;
    loop {
        // If our last log entry was a disconnect event, we add a timeout to the logging pipeline. If no logs come through
        // before the timeout, we assume the disconnect event is still relevant and retry connecting to the target.
        let (get_next_results, terminal_err) = if got_disconnect {
            match timeout(Duration::from_secs(5), run_logging_pipeline(&mut requests, &proxy)).await
            {
                Ok(tup) => tup,
                Err(_) => match retry_loop(&diagnostics_proxy, params.clone(), writer).await {
                    Ok(p) => {
                        proxy = p;
                        continue;
                    }
                    Err(e) => {
                        writeln!(writer, "Retry failed - trying again. Error was: {}", e)?;
                        continue;
                    }
                },
            }
        } else {
            run_logging_pipeline(&mut requests, &proxy).await
        };

        for result in get_next_results.into_iter() {
            got_disconnect = false;
            if let Err(e) = result {
                tracing::warn!("got an error from the daemon {:?}", e);
                log_formatter.push_log(Err(e)).await?;
                continue;
            }

            // The real data should always be in the diagnostics_data field so we throw away all
            // entries that have a None for that field and leave us an iterable of DiagnosticsData
            // types.
            let entries = result.unwrap().into_iter().filter_map(|e| e.diagnostics_data);

            for entry in entries {
                // There are two types of logs: small ones that fit inline in a message and long
                // ones that must be transported via a socket.
                // We deserialize the log entry directly from the inline variant or we fetch the
                // data by reading from the socket and then deserializing.
                let parsed = match entry {
                    DiagnosticsData::Inline(inline) => {
                        serde_json::from_str::<LogEntry>(&inline.data)?
                    }
                    DiagnosticsData::Socket(socket) => log_entry_from_socket(socket).await?,
                };
                got_disconnect = false;

                match (&parsed.data, to_bound_monotonic) {
                    (LogData::TargetLog(log_data), Some(t)) => {
                        let ts: i64 = log_data.metadata.timestamp.into();
                        if ts as u128 > t.as_nanos() {
                            return Ok(());
                        }
                    }
                    (LogData::FfxEvent(EventType::TargetDisconnected), _) => {
                        log_formatter.push_log(Ok(parsed)).await?;
                        // Rather than immediately attempt a retry here, we continue the loop. If neither of the
                        // outer loops have a log entry following this disconnect event, we will retry after attempting fetch
                        // subsequent logs from the backend.
                        got_disconnect = true;
                        continue;
                    }
                    _ => {}
                }

                log_formatter.push_log(Ok(parsed)).await?;
            }
        }

        if let Some(err) = terminal_err {
            tracing::info!("log command got a terminal error: {}", err);
            return Ok(());
        }
    }
}

async fn retry_loop<W: std::io::Write>(
    diagnostics_proxy: &DiagnosticsProxy,
    params: LogCommandParameters,
    writer: &mut W,
) -> Result<ArchiveIteratorProxy> {
    let mut fail_count = 0;
    loop {
        let (new_proxy, server) =
            create_proxy::<ArchiveIteratorMarker>().context("failed to create endpoints")?;
        match setup_diagnostics_stream(
            diagnostics_proxy,
            &params.target_identifier,
            server,
            params.stream_mode,
            params.from_bound.clone(),
            params.session.clone(),
        )
        .await?
        {
            Ok(_) => return Ok(new_proxy),
            Err(e) => {
                match e {
                    DiagnosticsStreamError::NoMatchingTargets => {
                        fail_count += 1;
                        write!(
                            writer,
                            "{}",
                            format_ffx_event(
                                &format!("{} isn't up. Retrying...", &params.target_identifier),
                                None
                            )
                        )?;
                    }
                    DiagnosticsStreamError::NoStreamForTarget => {
                        fail_count += 1;
                        write!(
                            writer,
                            "{}",
                            format_ffx_event(
                                &format!(
                                    "{} is up, but the logger hasn't started yet. Retrying...",
                                    &params.target_identifier
                                ),
                                None
                            )
                        )?;
                    }
                    _ => {
                        fail_count += 1;
                        write!(
                            writer,
                            "{}",
                            format_ffx_event(&format!("Retry failed: ({:?}).", e), None)
                        )?;
                    }
                }
                if fail_count > 5 {
                    writeln!(writer, "If this persists, consider restarting the ffx daemon with `ffx doctor --restart-daemon`.")?;
                } else {
                    writeln!(writer, "")?;
                }

                Timer::new(Duration::from_millis(RETRY_TIMEOUT_MILLIS)).await;
                continue;
            }
        }
    }
}

// This function drains the data from the passed in [socket] assuming it contains the bytes of a
// LogEntry object serialized to JSON.
async fn log_entry_from_socket(socket: fidl::Socket) -> Result<LogEntry> {
    let mut socket = fidl::AsyncSocket::from_socket(socket)
        .map_err(|e| anyhow!("failure to create async socket: {:?}", e))?;
    let mut result = Vec::new();
    let _ = socket
        .read_to_end(&mut result)
        .await
        .map_err(|e| anyhow!("failure to read log from socket: {:?}", e))?;
    let entry: LogEntry = serde_json::from_slice(&result)?;
    Ok(entry)
}

#[cfg(test)]
mod test {
    use {
        super::*,
        diagnostics_data::Timestamp,
        ffx_log_test_utils::{setup_fake_archive_iterator, FakeArchiveIteratorResponse},
        fidl_fuchsia_developer_ffx::{DiagnosticsMarker, DiagnosticsRequest},
        fidl_fuchsia_developer_remotecontrol::ArchiveIteratorError,
        futures::TryStreamExt,
        std::sync::Arc,
    };

    const DEFAULT_TS_NANOS: u64 = 1615535969000000000;
    const BOOT_TS: u64 = 98765432000000000;
    const START_TIMESTAMP_FOR_DAEMON: u64 = 1515903706000000000;

    fn default_ts() -> Duration {
        Duration::from_nanos(DEFAULT_TS_NANOS)
    }

    impl From<DaemonDiagnosticsStreamParameters> for LogCommandParameters {
        fn from(params: DaemonDiagnosticsStreamParameters) -> Self {
            Self {
                session: params.session,
                from_bound: params.min_timestamp_nanos,
                stream_mode: params.stream_mode.unwrap(),
                ..Self::default()
            }
        }
    }
    struct FakeLogFormatter {
        pushed_logs: Vec<ArchiveIteratorResult>,
    }

    #[async_trait(?Send)]
    impl LogFormatter for FakeLogFormatter {
        async fn push_log(&mut self, log_entry: ArchiveIteratorResult) -> Result<()> {
            self.pushed_logs.push(log_entry);
            Ok(())
        }

        fn set_boot_timestamp(&mut self, boot_ts_nanos: i64) {
            assert_eq!(boot_ts_nanos, BOOT_TS as i64)
        }
    }

    impl FakeLogFormatter {
        fn new() -> Self {
            Self { pushed_logs: vec![] }
        }

        fn assert_same_logs(&self, expected: Vec<ArchiveIteratorResult>) {
            assert_eq!(
                self.pushed_logs.len(),
                expected.len(),
                "got different number of log entries. \ngot: {:?}\nexpected: {:?}",
                self.pushed_logs,
                expected
            );
            for (got, expected_log) in self.pushed_logs.iter().zip(expected.iter()) {
                assert_eq!(
                    got, expected_log,
                    "got different log entries. \ngot: {:?}\nexpected: {:?}\n",
                    got, expected_log
                );
            }
        }
    }

    fn setup_fake_diagnostics_server(
        expected_parameters: DaemonDiagnosticsStreamParameters,
        expected_responses: Arc<Vec<FakeArchiveIteratorResponse>>,
    ) -> DiagnosticsProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<DiagnosticsMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    DiagnosticsRequest::StreamDiagnostics {
                        target,
                        parameters,
                        iterator,
                        responder,
                    } => {
                        assert_eq!(parameters, expected_parameters);
                        setup_fake_archive_iterator(iterator, expected_responses.clone(), false)
                            .unwrap();
                        responder
                            .send(&mut Ok(LogSession {
                                target_identifier: target,
                                session_timestamp_nanos: Some(BOOT_TS),
                                ..LogSession::EMPTY
                            }))
                            .context("error sending response")
                            .expect("should send")
                    }
                }
            }
        })
        .detach();
        proxy
    }

    fn make_log_entry(log_data: LogData) -> LogEntry {
        LogEntry {
            version: 1,
            timestamp: Timestamp::from(default_ts().as_nanos() as i64),
            data: log_data,
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dump_empty() {
        let mut formatter = FakeLogFormatter::new();
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotAll),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };
        let expected_responses = vec![];

        let mut writer = Vec::new();
        exec_log_cmd(
            LogCommandParameters::from(params.clone()),
            setup_fake_diagnostics_server(params, Arc::new(expected_responses)),
            &mut formatter,
            &mut writer,
        )
        .await
        .unwrap();

        let output = String::from_utf8(writer).unwrap();
        assert!(output.is_empty());

        formatter.assert_same_logs(vec![])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch() {
        let mut formatter = FakeLogFormatter::new();
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotRecentThenSubscribe),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };
        let log1 = make_log_entry(LogData::FfxEvent(EventType::LoggingStarted));
        let log2 = make_log_entry(LogData::MalformedTargetLog("text".to_string()));
        let log3 = make_log_entry(LogData::MalformedTargetLog("text2".to_string()));

        let expected_responses = vec![
            FakeArchiveIteratorResponse::new_with_values(vec![
                serde_json::to_string(&log1).unwrap(),
                serde_json::to_string(&log2).unwrap(),
            ]),
            FakeArchiveIteratorResponse::new_with_values(vec![
                serde_json::to_string(&log3).unwrap()
            ]),
        ];

        let mut writer = Vec::new();
        exec_log_cmd(
            LogCommandParameters::from(params.clone()),
            setup_fake_diagnostics_server(params, Arc::new(expected_responses)),
            &mut formatter,
            &mut writer,
        )
        .await
        .unwrap();

        let output = String::from_utf8(writer).unwrap();
        assert!(output.is_empty());
        formatter.assert_same_logs(vec![Ok(log1), Ok(log2), Ok(log3)])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch_no_dump_with_error() {
        let mut formatter = FakeLogFormatter::new();
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::Subscribe),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };
        let log1 = make_log_entry(LogData::FfxEvent(EventType::LoggingStarted));
        let log2 = make_log_entry(LogData::MalformedTargetLog("text".to_string()));
        let log3 = make_log_entry(LogData::MalformedTargetLog("text2".to_string()));

        let expected_responses = vec![
            FakeArchiveIteratorResponse::new_with_values(vec![
                serde_json::to_string(&log1).unwrap(),
                serde_json::to_string(&log2).unwrap(),
            ]),
            FakeArchiveIteratorResponse::new_with_error(ArchiveIteratorError::GenericError),
            FakeArchiveIteratorResponse::new_with_values(vec![
                serde_json::to_string(&log3).unwrap()
            ]),
        ];

        let mut writer = Vec::new();
        exec_log_cmd(
            LogCommandParameters::from(params.clone()),
            setup_fake_diagnostics_server(params, Arc::new(expected_responses)),
            &mut formatter,
            &mut writer,
        )
        .await
        .unwrap();

        let output = String::from_utf8(writer).unwrap();
        assert!(output.is_empty());
        formatter.assert_same_logs(vec![
            Ok(log1),
            Ok(log2),
            Err(ArchiveIteratorError::GenericError),
            Ok(log3),
        ])
    }
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_from_time_passed_to_daemon() {
        let mut formatter = FakeLogFormatter::new();
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotAll),
            min_timestamp_nanos: Some(TimeBound::Absolute(START_TIMESTAMP_FOR_DAEMON)),
            session: Some(SessionSpec::Relative(0)),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };

        let mut writer = Vec::new();
        exec_log_cmd(
            LogCommandParameters::from(params.clone()),
            setup_fake_diagnostics_server(params, Arc::new(vec![])),
            &mut formatter,
            &mut writer,
        )
        .await
        .unwrap();

        let output = String::from_utf8(writer).unwrap();
        assert!(output.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_from_monotonic_passed_to_daemon() {
        let mut formatter = FakeLogFormatter::new();
        let params = DaemonDiagnosticsStreamParameters {
            stream_mode: Some(StreamMode::SnapshotAll),
            min_timestamp_nanos: Some(TimeBound::Monotonic(default_ts().as_nanos() as u64)),
            session: Some(SessionSpec::Relative(0)),
            ..DaemonDiagnosticsStreamParameters::EMPTY
        };

        let mut writer = Vec::new();
        exec_log_cmd(
            LogCommandParameters::from(params.clone()),
            setup_fake_diagnostics_server(params, Arc::new(vec![])),
            &mut formatter,
            &mut writer,
        )
        .await
        .unwrap();

        let output = String::from_utf8(writer).unwrap();
        assert!(output.is_empty());
    }
}
