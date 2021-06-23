// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Error, Result},
    diagnostics_data::{Logs, LogsData},
    diagnostics_reader::{ArchiveReader, Error as ReaderError},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorEntry, ArchiveIteratorError, ArchiveIteratorMarker, ArchiveIteratorRequest,
        BridgeStreamParameters, DiagnosticsData, InlineData, RemoteDiagnosticsBridgeRequest,
        RemoteDiagnosticsBridgeRequestStream, StreamError,
    },
    fidl_fuchsia_diagnostics::{DataType, StreamMode},
    fidl_fuchsia_logger::MAX_DATAGRAM_LEN_BYTES,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{
        prelude::*,
        select,
        stream::{FusedStream, TryStreamExt},
    },
    std::cmp::max,
    std::convert::TryInto,
    std::fmt::Debug,
    std::sync::Arc,
    tracing::{info, warn},
};

// This lets us mock out the ArchiveReader in tests
pub trait ArchiveReaderManager {
    type Error: Debug + Send + 'static;

    fn start_log_stream(
        &mut self,
    ) -> Result<
        Box<dyn FusedStream<Item = Result<LogsData, Self::Error>> + Unpin + Send>,
        StreamError,
    >;

    /// Provides an implementation of an ArchiveIterator server. Intended to be used by clients who
    /// wish to spawn an ArchiveIterator server given an implementation of `start_log_stream`..
    fn spawn_iterator_server(
        &mut self,
        iterator: ServerEnd<ArchiveIteratorMarker>,
    ) -> Result<fasync::Task<Result<(), Error>>, StreamError> {
        let stream_result = match self.start_log_stream() {
            Ok(r) => r,
            Err(e) => return Err(e),
        };
        let task = fasync::Task::spawn(async move {
            let mut iter_stream = iterator.into_stream()?;
            let mut result_stream = stream_result;

            while let Some(request) = iter_stream.next().await {
                match request? {
                    ArchiveIteratorRequest::GetNext { responder } => {
                        let logs = select! {
                            result = result_stream.select_next_some() => {
                                match result {
                                    Err(err) => {
                                        warn!(?err, "Data read error");
                                        responder.send(&mut Err(
                                            ArchiveIteratorError::DataReadFailed))?;
                                        continue;
                                    }
                                    Ok(log) => log,
                                }
                            }

                            complete => {
                                responder.send(&mut Ok(vec![]))?;
                                break;
                            }
                        };

                        let (truncated_logs, truncated_chars) = match truncate_log_msg(logs) {
                            Ok(t) => t,
                            Err(err) => {
                                warn!(?err, "failed to truncate log message");
                                responder.send(&mut Err(ArchiveIteratorError::TruncationFailed))?;
                                continue;
                            }
                        };

                        let response = vec![ArchiveIteratorEntry {
                            diagnostics_data: Some(DiagnosticsData::Inline(InlineData {
                                data: serde_json::to_string(&truncated_logs)?,
                                truncated_chars: truncated_chars,
                            })),
                            ..ArchiveIteratorEntry::EMPTY
                        }];
                        responder.send(&mut Ok(response))?;
                    }
                }
            }

            Ok::<(), Error>(())
        });
        Ok(task)
    }
}

struct ArchiveReaderManagerImpl {
    reader: ArchiveReader,
}

impl ArchiveReaderManagerImpl {
    fn new(_parameters: BridgeStreamParameters) -> Self {
        // TODO(jwing): use parameters once we support more of them.
        let reader = ArchiveReader::new();
        Self { reader: reader }
    }
}

impl ArchiveReaderManager for ArchiveReaderManagerImpl {
    type Error = ReaderError;

    fn start_log_stream(
        &mut self,
    ) -> Result<
        Box<dyn FusedStream<Item = Result<LogsData, Self::Error>> + Unpin + Send>,
        StreamError,
    > {
        let result_stream = self.reader.snapshot_then_subscribe::<Logs>().map_err(|err| {
            warn!(%err, "Got error creating log subscription");
            StreamError::SetupSubscriptionFailed
        })?;

        Ok(Box::new(result_stream))
    }
}

fn truncate_to_char_boundary(s: &str, mut max_bytes: usize) -> &str {
    if max_bytes >= s.len() {
        s
    } else {
        while !s.is_char_boundary(max_bytes) {
            max_bytes -= 1;
        }
        &s[..max_bytes]
    }
}

fn truncate_log_msg(mut logs: LogsData) -> Result<(LogsData, u32)> {
    let msg_mut = logs.msg_mut().ok_or(anyhow!("missing log message"))?;
    let orig_len: u32 = msg_mut.len().try_into()?;
    let new_msg = truncate_to_char_boundary(msg_mut, MAX_DATAGRAM_LEN_BYTES as usize);
    *msg_mut = new_msg.to_string();
    let truncated_chars =
        if MAX_DATAGRAM_LEN_BYTES > orig_len { 0 } else { orig_len - MAX_DATAGRAM_LEN_BYTES };
    return Ok((logs, max(0, truncated_chars).try_into()?));
}

pub struct RemoteDiagnosticsBridge<E, F>
where
    F: Fn(BridgeStreamParameters) -> Box<dyn ArchiveReaderManager<Error = E>>,
    E: Into<anyhow::Error> + Send,
{
    archive_accessor: F,
}

impl<E, F> RemoteDiagnosticsBridge<E, F>
where
    F: Fn(BridgeStreamParameters) -> Box<dyn ArchiveReaderManager<Error = E>>
        + std::marker::Send
        + std::marker::Sync
        + 'static,
    E: Into<anyhow::Error> + Send + Debug + 'static,
{
    pub fn new(archive_accessor: F) -> Result<Self> {
        return Ok(RemoteDiagnosticsBridge { archive_accessor: archive_accessor });
    }

    fn validate_params(&self, parameters: &BridgeStreamParameters) -> Result<(), StreamError> {
        match parameters.data_type {
            Some(DataType::Logs) => {}
            None => return Err(StreamError::MissingParameter),
            _ => return Err(StreamError::UnsupportedParameter),
        }

        match parameters.stream_mode {
            Some(StreamMode::SnapshotThenSubscribe) => {}
            None => return Err(StreamError::MissingParameter),
            _ => return Err(StreamError::UnsupportedParameter),
        }

        Ok(())
    }

    pub async fn serve_stream(
        &self,
        mut stream: RemoteDiagnosticsBridgeRequestStream,
    ) -> Result<()> {
        while let Some(request) =
            stream.try_next().await.context("next RemoteDiagnosticsBridge request")?
        {
            match request {
                RemoteDiagnosticsBridgeRequest::StreamDiagnostics {
                    parameters,
                    iterator,
                    responder,
                } => {
                    info!("Kicking off a new log stream.");

                    if let Err(e) = self.validate_params(&parameters) {
                        responder.send(&mut Err(e))?;
                        continue;
                    }

                    let mut reader = (self.archive_accessor)(parameters);
                    match reader.spawn_iterator_server(iterator) {
                        Ok(task) => {
                            responder.send(&mut Ok(()))?;
                            task.await?;
                        }
                        Err(e) => {
                            responder.send(&mut Err(e))?;
                        }
                    }
                }
                RemoteDiagnosticsBridgeRequest::Hello { responder } => {
                    responder.send()?;
                }
            }
        }
        Ok(())
    }
}

pub async fn exec_server() -> Result<()> {
    fuchsia_syslog::init_with_tags(&["remote-diagnostics-bridge"])?;
    info!("starting log-reader");
    let service =
        Arc::new(RemoteDiagnosticsBridge::new(|p| Box::new(ArchiveReaderManagerImpl::new(p)))?);

    let sc1 = service.clone();
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |req| {
        let sc = sc1.clone();
        fasync::Task::local(async move {
            sc.clone().serve_stream(req).map(|_| ()).await;
        })
        .detach();
    });

    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;

    Ok(())
}

#[cfg(test)]
mod test {
    use {
        super::*,
        anyhow::Error,
        diagnostics_data::Severity,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_developer_remotecontrol::{
            ArchiveIteratorMarker, RemoteDiagnosticsBridgeMarker, RemoteDiagnosticsBridgeProxy,
        },
        fuchsia_async as fasync,
        futures::stream::iter,
        matches::assert_matches,
        std::cell::Cell,
    };

    const LONG_LOG_LEN: u32 = MAX_DATAGRAM_LEN_BYTES * 2;

    struct FakeArchiveReaderManager {
        logs: Vec<LogsData>,
        errors: Cell<Vec<Error>>,
        connection_error: Option<StreamError>,
    }

    impl FakeArchiveReaderManager {
        fn new(_parameters: BridgeStreamParameters) -> Self {
            return Self { logs: vec![], connection_error: None, errors: Cell::new(vec![]) };
        }

        fn new_with_data(
            _parameters: BridgeStreamParameters,
            logs: Vec<LogsData>,
            errors: Vec<Error>,
        ) -> Self {
            return Self { logs, connection_error: None, errors: Cell::new(errors) };
        }

        fn new_with_failed_connect(
            _parameters: BridgeStreamParameters,
            error: StreamError,
        ) -> Self {
            return Self { logs: vec![], errors: Cell::new(vec![]), connection_error: Some(error) };
        }
    }

    impl ArchiveReaderManager for FakeArchiveReaderManager {
        type Error = anyhow::Error;

        fn start_log_stream(
            &mut self,
        ) -> Result<
            Box<dyn FusedStream<Item = Result<LogsData, Self::Error>> + Unpin + Send>,
            StreamError,
        > {
            if let Some(cerr) = self.connection_error {
                return Err(cerr);
            }

            let (mut sender, rec) =
                futures::channel::mpsc::channel::<Result<LogsData, Self::Error>>(0);
            let logs = self.logs.clone();
            let errors = self.errors.get_mut().drain(..).collect::<Vec<Error>>();
            fasync::Task::local(async move {
                sender.send_all(&mut iter(errors.into_iter().map(|e| Ok(Err(e))))).await.unwrap();
                for log in logs {
                    sender.send(Ok(log)).await.unwrap();
                }
            })
            .detach();
            Ok(Box::new(rec.fuse()))
        }
    }

    fn setup_diagnostics_bridge_proxy<F>(service: F) -> RemoteDiagnosticsBridgeProxy
    where
        F: Fn(BridgeStreamParameters) -> Box<dyn ArchiveReaderManager<Error = anyhow::Error>>
            + std::marker::Send
            + std::marker::Sync
            + 'static,
    {
        let service = Arc::new(RemoteDiagnosticsBridge::new(service).unwrap());
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteDiagnosticsBridgeMarker>().unwrap();
        fasync::Task::local(async move {
            service.serve_stream(stream).await.unwrap();
        })
        .detach();

        return proxy;
    }

    fn make_log(timestamp: i64, msg: String) -> LogsData {
        diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
            timestamp_nanos: timestamp.into(),
            component_url: String::from("fake-url"),
            moniker: String::from("test/moniker"),
            severity: Severity::Error,
            size_bytes: 1,
        })
        .set_message(msg)
        .build()
    }

    fn make_short_log(timestamp: i64) -> LogsData {
        make_log(timestamp, "msg".to_string())
    }

    fn make_long_log(timestamp: i64, len: u32) -> LogsData {
        let mut msg = String::default();
        for _ in 0..len {
            msg.push('a');
        }

        make_log(timestamp, msg)
    }

    fn default_log_parameters() -> BridgeStreamParameters {
        BridgeStreamParameters {
            data_type: Some(DataType::Logs),
            stream_mode: Some(StreamMode::SnapshotThenSubscribe),
            ..BridgeStreamParameters::EMPTY
        }
    }

    #[test]
    fn test_truncate() {
        let s = "eichhörnchen";
        assert_eq!(truncate_to_char_boundary(s, 1), "e");
        assert_eq!(truncate_to_char_boundary(s, 5), "eichh");
        assert_eq!(truncate_to_char_boundary(s, 6), "eichh");
        assert_eq!(truncate_to_char_boundary(s, 7), "eichhö");
        assert_eq!(truncate_to_char_boundary(s, 100), "eichhörnchen");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_empty_log_stream() {
        let proxy = setup_diagnostics_bridge_proxy(|p| Box::new(FakeArchiveReaderManager::new(p)));
        let (iterator, server) = create_proxy::<ArchiveIteratorMarker>().unwrap();

        proxy.stream_diagnostics(default_log_parameters(), server).await.unwrap().unwrap();

        let entries = iterator.get_next().await.unwrap().expect("expected Ok response");
        assert!(entries.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_missing_data_type() {
        let proxy = setup_diagnostics_bridge_proxy(|p| Box::new(FakeArchiveReaderManager::new(p)));
        let (_, server) = create_proxy::<ArchiveIteratorMarker>().unwrap();

        let err = proxy
            .stream_diagnostics(
                BridgeStreamParameters {
                    data_type: None,
                    stream_mode: Some(StreamMode::SnapshotThenSubscribe),
                    ..BridgeStreamParameters::EMPTY
                },
                server,
            )
            .await
            .unwrap()
            .unwrap_err();

        assert_eq!(err, StreamError::MissingParameter);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_missing_stream_mode() {
        let proxy = setup_diagnostics_bridge_proxy(|p| Box::new(FakeArchiveReaderManager::new(p)));
        let (_, server) = create_proxy::<ArchiveIteratorMarker>().unwrap();

        let err = proxy
            .stream_diagnostics(
                BridgeStreamParameters {
                    data_type: Some(DataType::Logs),
                    stream_mode: None,
                    ..BridgeStreamParameters::EMPTY
                },
                server,
            )
            .await
            .unwrap()
            .unwrap_err();

        assert_eq!(err, StreamError::MissingParameter);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unsupported_data_type() {
        let proxy = setup_diagnostics_bridge_proxy(|p| Box::new(FakeArchiveReaderManager::new(p)));
        let (_, server) = create_proxy::<ArchiveIteratorMarker>().unwrap();

        let err = proxy
            .stream_diagnostics(
                BridgeStreamParameters {
                    data_type: Some(DataType::Inspect),
                    stream_mode: Some(StreamMode::SnapshotThenSubscribe),
                    ..BridgeStreamParameters::EMPTY
                },
                server,
            )
            .await
            .unwrap()
            .unwrap_err();

        assert_eq!(err, StreamError::UnsupportedParameter);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unsupported_stream_mode() {
        let proxy = setup_diagnostics_bridge_proxy(|p| Box::new(FakeArchiveReaderManager::new(p)));
        let (_, server) = create_proxy::<ArchiveIteratorMarker>().unwrap();

        let err = proxy
            .stream_diagnostics(
                BridgeStreamParameters {
                    data_type: Some(DataType::Logs),
                    stream_mode: Some(StreamMode::Snapshot),
                    ..BridgeStreamParameters::EMPTY
                },
                server,
            )
            .await
            .unwrap()
            .unwrap_err();

        assert_eq!(err, StreamError::UnsupportedParameter);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_two_log_stream_no_errors() {
        let proxy = setup_diagnostics_bridge_proxy(|p| {
            Box::new(FakeArchiveReaderManager::new_with_data(
                p,
                vec![make_short_log(1), make_short_log(2)],
                vec![],
            ))
        });
        let (iterator, server) = create_proxy::<ArchiveIteratorMarker>().unwrap();

        proxy
            .stream_diagnostics(default_log_parameters(), server)
            .await
            .unwrap()
            .expect("expect Ok response");

        let entries = iterator.get_next().await.unwrap().expect("get next should not error");

        assert_eq!(entries.len(), 1);
        let entry = entries.get(0).unwrap();
        let diagnostics_data = entry.diagnostics_data.as_ref().unwrap();
        assert_matches!(diagnostics_data, DiagnosticsData::Inline(_));
        if let DiagnosticsData::Inline(inline) = diagnostics_data {
            assert_eq!(inline.truncated_chars, 0);
            assert_eq!(inline.data, serde_json::to_string(&make_short_log(1)).unwrap());
        }

        let entries = iterator.get_next().await.unwrap().expect("get next should not error");
        assert_eq!(entries.len(), 1);
        let entry = entries.get(0).unwrap();
        let diagnostics_data = entry.diagnostics_data.as_ref().unwrap();
        assert_matches!(diagnostics_data, DiagnosticsData::Inline(_));
        if let DiagnosticsData::Inline(inline) = diagnostics_data {
            assert_eq!(inline.truncated_chars, 0);
            assert_eq!(inline.data, serde_json::to_string(&make_short_log(2)).unwrap());
        }

        let empty_entries = iterator.get_next().await.unwrap().expect("get next should not error");
        assert!(empty_entries.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_two_log_stream_with_errors() {
        let proxy = setup_diagnostics_bridge_proxy(|p| {
            Box::new(FakeArchiveReaderManager::new_with_data(
                p,
                vec![make_short_log(1)],
                vec![anyhow!("error1"), anyhow!("error2")],
            ))
        });
        let (iterator, server) = create_proxy::<ArchiveIteratorMarker>().unwrap();

        proxy
            .stream_diagnostics(default_log_parameters(), server)
            .await
            .unwrap()
            .expect("expect Ok response");

        let result = iterator.get_next().await.unwrap();
        assert_eq!(result.unwrap_err(), ArchiveIteratorError::DataReadFailed);

        let result = iterator.get_next().await.unwrap();
        assert_eq!(result.unwrap_err(), ArchiveIteratorError::DataReadFailed);

        let entries = iterator.get_next().await.unwrap().expect("get next should not error");
        assert_eq!(entries.len(), 1);
        let entry = entries.get(0).unwrap();
        let diagnostics_data = entry.diagnostics_data.as_ref().unwrap();
        assert_matches!(diagnostics_data, DiagnosticsData::Inline(_));
        if let DiagnosticsData::Inline(inline) = diagnostics_data {
            assert_eq!(inline.truncated_chars, 0);
            assert_eq!(inline.data.clone(), serde_json::to_string(&make_short_log(1)).unwrap());
        }

        let empty_entries = iterator.get_next().await.unwrap().expect("get next should not error");
        assert!(empty_entries.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_connection_fails() {
        let proxy = setup_diagnostics_bridge_proxy(|p| {
            Box::new(FakeArchiveReaderManager::new_with_failed_connect(
                p,
                StreamError::SetupSubscriptionFailed,
            ))
        });
        let (_, server) = create_proxy::<ArchiveIteratorMarker>().unwrap();

        let result = proxy
            .stream_diagnostics(default_log_parameters(), server)
            .await
            .unwrap()
            .expect_err("connection should fail");

        assert_eq!(result, StreamError::SetupSubscriptionFailed);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_truncates_long_message() {
        let proxy = setup_diagnostics_bridge_proxy(|p| {
            Box::new(FakeArchiveReaderManager::new_with_data(
                p,
                vec![make_long_log(1, LONG_LOG_LEN)],
                vec![],
            ))
        });
        let (iterator, server) = create_proxy::<ArchiveIteratorMarker>().unwrap();

        proxy
            .stream_diagnostics(default_log_parameters(), server)
            .await
            .unwrap()
            .expect("expect Ok response");

        let entries = iterator.get_next().await.unwrap().expect("get next should not error");
        assert_eq!(entries.len(), 1);
        let entry = entries.get(0).unwrap();
        let diagnostics_data = entry.diagnostics_data.as_ref().unwrap();
        assert_matches!(diagnostics_data, DiagnosticsData::Inline(_));
        if let DiagnosticsData::Inline(inline) = diagnostics_data {
            let data: LogsData = serde_json::from_str(inline.data.as_ref()).unwrap();
            let expected_data = make_long_log(1, MAX_DATAGRAM_LEN_BYTES);
            assert_eq!(inline.truncated_chars, MAX_DATAGRAM_LEN_BYTES);
            assert_eq!(data, expected_data);
        }

        let empty_entries = iterator.get_next().await.unwrap().expect("get next should not error");
        assert!(empty_entries.is_empty());
    }
}
