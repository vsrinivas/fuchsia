// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Error, Result},
    async_trait::async_trait,
    diagnostics_data::{Data, Inspect, Logs, LogsData},
    diagnostics_reader::{ArchiveReader, Error as ReaderError},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorEntry, ArchiveIteratorError, ArchiveIteratorMarker, ArchiveIteratorRequest,
        BridgeStreamParameters, DiagnosticsData, InlineData, RemoteDiagnosticsBridgeRequest,
        RemoteDiagnosticsBridgeRequestStream, StreamError,
    },
    fidl_fuchsia_diagnostics::{
        ArchiveAccessorProxy,
        ClientSelectorConfiguration::{SelectAll, Selectors},
        DataType, SelectorArgument, StreamMode,
    },
    fidl_fuchsia_logger::MAX_DATAGRAM_LEN_BYTES,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{
        future::BoxFuture,
        prelude::*,
        select,
        stream::{FusedStream, TryStreamExt},
    },
    iquery::commands::connect_to_archive_accessor,
    selectors,
    std::fmt::Debug,
    std::sync::Arc,
    tracing::{error, info, warn},
};

// This lets us mock out the ArchiveReader in tests
#[async_trait]
pub trait ArchiveReaderManager {
    type Error: Debug + Send + 'static;

    async fn snapshot<D: diagnostics_data::DiagnosticsData + 'static>(
        &self,
    ) -> Result<Vec<Data<D>>, StreamError>;

    async fn run_snapshot_server<D: diagnostics_data::DiagnosticsData + 'static>(
        &self,
        data: Vec<Data<D>>,
        iterator: ServerEnd<ArchiveIteratorMarker>,
    ) -> Result<(), Error> {
        let mut is_sent = false;
        let mut iter_stream = iterator.into_stream()?;
        while let Some(request) = iter_stream.next().await {
            let ArchiveIteratorRequest::GetNext { responder } = request?;
            if is_sent {
                responder.send(&mut Ok(vec![]))?;
                continue;
            }
            is_sent = true;
            let data = serde_json::to_string(&data)?;
            if data.len() <= MAX_DATAGRAM_LEN_BYTES as usize {
                let response = vec![ArchiveIteratorEntry {
                    diagnostics_data: Some(DiagnosticsData::Inline(InlineData {
                        data,
                        truncated_chars: 0,
                    })),
                    ..ArchiveIteratorEntry::EMPTY
                }];
                responder.send(&mut Ok(response))?;
            } else {
                let sock_opts = fuchsia_zircon::SocketOpts::STREAM;
                let (socket, tx_socket) = fuchsia_zircon::Socket::create(sock_opts)?;
                let mut tx_socket = fasync::Socket::from_socket(tx_socket)?;
                let response = vec![ArchiveIteratorEntry {
                    diagnostics_data: Some(DiagnosticsData::Socket(socket)),
                    ..ArchiveIteratorEntry::EMPTY
                }];
                responder.send(&mut Ok(response))?;
                tx_socket.write_all(data.as_bytes()).await?;
            }
        }
        Ok(())
    }

    fn start_log_stream(
        &mut self,
    ) -> Result<
        Box<dyn FusedStream<Item = Result<LogsData, Self::Error>> + Unpin + Send>,
        StreamError,
    >;

    /// Provides an implementation of an ArchiveIterator server. Intended to be used by clients who
    /// wish to run an ArchiveIterator server given an implementation of `start_log_stream`..
    fn run_iterator_server(
        &mut self,
        iterator: ServerEnd<ArchiveIteratorMarker>,
    ) -> Result<BoxFuture<'static, Result<(), Error>>, StreamError> {
        let stream_result = match self.start_log_stream() {
            Ok(r) => r,
            Err(e) => return Err(e),
        };
        let future = async move {
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

                        let send_inline = logs_data_can_be_sent_inline(&logs);
                        let data = serde_json::to_string(&logs)?;
                        if send_inline {
                            // Short data => send the data within the message.
                            let response = vec![ArchiveIteratorEntry {
                                diagnostics_data: Some(DiagnosticsData::Inline(InlineData {
                                    data,
                                    truncated_chars: 0,
                                })),
                                ..ArchiveIteratorEntry::EMPTY
                            }];
                            responder.send(&mut Ok(response))?;
                        } else {
                            // Long data => create a socket, send the socket in the message, then
                            // write all the data on the other side of the socket.
                            let sock_opts = fuchsia_zircon::SocketOpts::STREAM;
                            let (socket, tx_socket) = fuchsia_zircon::Socket::create(sock_opts)?;
                            let mut tx_socket = fasync::Socket::from_socket(tx_socket)?;
                            let response = vec![ArchiveIteratorEntry {
                                diagnostics_data: Some(DiagnosticsData::Socket(socket)),
                                ..ArchiveIteratorEntry::EMPTY
                            }];
                            responder.send(&mut Ok(response))?;
                            tx_socket.write_all(data.as_bytes()).await?;
                        }
                    }
                }
            }

            Ok::<(), Error>(())
        }
        .boxed();
        Ok(future)
    }
}

struct ArchiveReaderManagerImpl {
    reader: ArchiveReader,
}

impl ArchiveReaderManagerImpl {
    async fn new(parameters: BridgeStreamParameters) -> Result<Self> {
        let archive: ArchiveAccessorProxy =
            connect_to_archive_accessor(&parameters.accessor_path).await?;
        let mut reader = ArchiveReader::new();
        reader.with_archive(archive).retry_if_empty(false);
        match parameters.client_selector_configuration {
            Some(Selectors(s)) => {
                for selector in s.into_iter() {
                    let selector = match selector {
                        SelectorArgument::StructuredSelector(s) => {
                            selectors::selector_to_string(s)?
                        }
                        SelectorArgument::RawSelector(s) => s,
                        _ => return Err(anyhow!("unknown selector argument")),
                    };
                    reader.add_selector(selector);
                }
            }
            Some(SelectAll(true)) => {}
            _ => return Err(anyhow!("unsupported selector arguments")),
        }
        Ok(Self { reader })
    }
}

#[async_trait]
impl ArchiveReaderManager for ArchiveReaderManagerImpl {
    type Error = ReaderError;

    async fn snapshot<D: diagnostics_data::DiagnosticsData + 'static>(
        &self,
    ) -> Result<Vec<Data<D>>, StreamError> {
        self.reader.snapshot::<D>().await.map_err(|err| {
            warn!(%err, "Got error getting snapshot");
            StreamError::SetupSubscriptionFailed
        })
    }

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

// This checks whether the log can fit within a FIDL message to be able to use the inline variant
// of the DiagnosticsData type.
fn logs_data_can_be_sent_inline(logs: &LogsData) -> bool {
    logs.msg().map(|msg| msg.len() <= MAX_DATAGRAM_LEN_BYTES as usize).unwrap_or(true)
}

pub struct RemoteDiagnosticsBridge<E, F, A, Fut>
where
    F: Fn(BridgeStreamParameters) -> Fut,
    E: Into<anyhow::Error> + Send,
    A: ArchiveReaderManager<Error = E> + Send,
    Fut: Future<Output = Result<A>>,
{
    archive_accessor: F,
}

impl<E, F, A, Fut> RemoteDiagnosticsBridge<E, F, A, Fut>
where
    F: Fn(BridgeStreamParameters) -> Fut + Send + Sync + 'static,
    E: Into<anyhow::Error> + Send + Debug + 'static,
    A: ArchiveReaderManager<Error = E> + Send + Sync,
    Fut: Future<Output = Result<A>>,
{
    pub fn new(archive_accessor: F) -> Result<Self> {
        return Ok(RemoteDiagnosticsBridge { archive_accessor });
    }

    fn validate_params(
        &self,
        parameters: &BridgeStreamParameters,
    ) -> Result<(DataType, StreamMode), StreamError> {
        match (parameters.data_type, parameters.stream_mode) {
            (Some(d), Some(s @ StreamMode::Snapshot)) => Ok((d, s)),
            (Some(d @ DataType::Logs), Some(s @ StreamMode::SnapshotThenSubscribe)) => Ok((d, s)),
            (None, _) => Err(StreamError::MissingParameter),
            (_, None) => Err(StreamError::MissingParameter),
            (_, _) => Err(StreamError::UnsupportedParameter),
        }
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
                    let (data_type, stream_mode) = match self.validate_params(&parameters) {
                        Err(e) => {
                            responder.send(&mut Err(e))?;
                            continue;
                        }
                        Ok(v) => v,
                    };

                    let mut reader = (self.archive_accessor)(parameters).await?;
                    let res = match stream_mode {
                        StreamMode::SnapshotThenSubscribe => reader.run_iterator_server(iterator),
                        StreamMode::Snapshot => match data_type {
                            DataType::Logs => reader
                                .snapshot::<Logs>()
                                .await
                                .map(|data| reader.run_snapshot_server(data, iterator).boxed()),
                            DataType::Inspect => reader
                                .snapshot::<Inspect>()
                                .await
                                .map(|data| reader.run_snapshot_server(data, iterator).boxed()),
                            DataType::Lifecycle => {
                                unreachable!("Lifecycle data is being deprecated")
                            }
                        },
                        _ => {
                            responder.send(&mut Err(StreamError::UnsupportedParameter))?;
                            continue;
                        }
                    };
                    match res {
                        Ok(fut) => {
                            responder.send(&mut Ok(()))?;
                            fut.await?;
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
    diagnostics_log::init!(&["remote-diagnostics-bridge"]);
    info!("starting log-reader");
    let service = Arc::new(RemoteDiagnosticsBridge::new(|p| ArchiveReaderManagerImpl::new(p))?);

    let sc1 = service.clone();
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |req| {
        let sc = sc1.clone();
        fasync::Task::local(async move {
            match sc.clone().serve_stream(req).await {
                Ok(()) => {}
                Err(e) => {
                    error!(%e, "error encountered while serving stream");
                }
            }
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
        assert_matches::assert_matches,
        async_trait::async_trait,
        diagnostics_data::{DiagnosticsHierarchy, InspectData, Property, Severity},
        fidl::endpoints::create_proxy,
        fidl::AsyncSocket,
        fidl_fuchsia_developer_remotecontrol::{
            ArchiveIteratorMarker, RemoteDiagnosticsBridgeMarker, RemoteDiagnosticsBridgeProxy,
        },
        fuchsia_async as fasync,
        futures::stream::iter,
    };

    const LONG_LOG_LEN: u32 = MAX_DATAGRAM_LEN_BYTES * 2;

    struct FakeArchiveReaderManager {
        logs: Vec<LogsData>,
        inspect: Vec<InspectData>,
        errors: Vec<Error>,
        connection_error: Option<StreamError>,
    }

    impl FakeArchiveReaderManager {
        fn new(_parameters: BridgeStreamParameters) -> Result<Self> {
            return Ok(Self {
                logs: vec![],
                inspect: vec![],
                connection_error: None,
                errors: vec![],
            });
        }

        fn new_with_data(
            _parameters: BridgeStreamParameters,
            logs: Vec<LogsData>,
            inspect: Vec<InspectData>,
            errors: Vec<Error>,
        ) -> Result<Self> {
            return Ok(Self { logs, inspect, connection_error: None, errors });
        }

        fn new_with_failed_connect(
            _parameters: BridgeStreamParameters,
            error: StreamError,
        ) -> Result<Self> {
            return Ok(Self {
                logs: vec![],
                inspect: vec![],
                errors: vec![],
                connection_error: Some(error),
            });
        }
    }

    #[async_trait]
    impl ArchiveReaderManager for FakeArchiveReaderManager {
        type Error = anyhow::Error;

        async fn snapshot<D: diagnostics_data::DiagnosticsData + 'static>(
            &self,
        ) -> Result<Vec<Data<D>>, StreamError> {
            if let Some(cerr) = self.connection_error {
                return Err(cerr);
            }
            match D::DATA_TYPE {
                DataType::Inspect => {
                    let data = self.inspect.clone();
                    let data = async move { data }.await;
                    let data_str = serde_json::to_string(&data).unwrap();
                    let result = serde_json::from_str(&data_str).unwrap();
                    Ok(result)
                }
                DataType::Logs => {
                    let data = self.logs.clone();
                    let data = async move { data }.await;
                    let data_str = serde_json::to_string(&data).unwrap();
                    let result = serde_json::from_str(&data_str).unwrap();
                    Ok(result)
                }
                DataType::Lifecycle => {
                    unreachable!("Lifecycle data is being deprecated")
                }
            }
        }

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
            let errors = self.errors.drain(..).collect::<Vec<Error>>();
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

    fn setup_diagnostics_bridge_proxy<F, A, Fut>(service: F) -> RemoteDiagnosticsBridgeProxy
    where
        F: Fn(BridgeStreamParameters) -> Fut + std::marker::Send + std::marker::Sync + 'static,
        A: ArchiveReaderManager<Error = anyhow::Error> + std::marker::Send + Sync + 'static,
        Fut: Future<Output = Result<A>> + 'static,
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
            component_url: Some(String::from("fake-url")),
            moniker: String::from("test/moniker"),
            severity: Severity::Error,
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

    fn make_long_inspect(timestamp: i64) -> InspectData {
        let long_string = std::iter::repeat("a").take(LONG_LOG_LEN as usize).collect::<String>();
        let hierarchy = DiagnosticsHierarchy::new(
            String::from("name"),
            vec![Property::String(String::from("hello"), long_string)],
            vec![],
        );
        Data::for_inspect(
            String::from("test/moniker"),
            Some(hierarchy),
            timestamp,
            String::from("fake-url"),
            String::from("fake-filename"),
            vec![],
        )
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_empty_log_stream() {
        let proxy = setup_diagnostics_bridge_proxy(|p| async { FakeArchiveReaderManager::new(p) });
        let (iterator, server) = create_proxy::<ArchiveIteratorMarker>().unwrap();

        proxy.stream_diagnostics(default_log_parameters(), server).await.unwrap().unwrap();

        let entries = iterator.get_next().await.unwrap().expect("expected Ok response");
        assert!(entries.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_missing_data_type() {
        let proxy = setup_diagnostics_bridge_proxy(|p| async { FakeArchiveReaderManager::new(p) });
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
        let proxy = setup_diagnostics_bridge_proxy(|p| async { FakeArchiveReaderManager::new(p) });
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
    async fn test_unsupported_data_type_for_subscribe() {
        let proxy = setup_diagnostics_bridge_proxy(|p| async { FakeArchiveReaderManager::new(p) });
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
        let proxy = setup_diagnostics_bridge_proxy(|p| async { FakeArchiveReaderManager::new(p) });
        let (_, server) = create_proxy::<ArchiveIteratorMarker>().unwrap();

        let err = proxy
            .stream_diagnostics(
                BridgeStreamParameters {
                    data_type: Some(DataType::Logs),
                    stream_mode: Some(StreamMode::Subscribe),
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
        let proxy = setup_diagnostics_bridge_proxy(|p| async {
            FakeArchiveReaderManager::new_with_data(
                p,
                vec![make_short_log(1), make_short_log(2)],
                vec![],
                vec![],
            )
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
        let proxy = setup_diagnostics_bridge_proxy(|p| async {
            FakeArchiveReaderManager::new_with_data(
                p,
                vec![make_short_log(1)],
                vec![],
                vec![anyhow!("error1"), anyhow!("error2")],
            )
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
        let proxy = setup_diagnostics_bridge_proxy(|p| async {
            FakeArchiveReaderManager::new_with_failed_connect(
                p,
                StreamError::SetupSubscriptionFailed,
            )
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
    async fn test_long_message_is_sent_with_a_socket() {
        let proxy = setup_diagnostics_bridge_proxy(|p| async {
            FakeArchiveReaderManager::new_with_data(
                p,
                vec![make_long_log(1, LONG_LOG_LEN)],
                vec![],
                vec![],
            )
        });
        let (iterator, server) = create_proxy::<ArchiveIteratorMarker>().unwrap();

        proxy
            .stream_diagnostics(default_log_parameters(), server)
            .await
            .unwrap()
            .expect("expect Ok response");

        let mut entries = iterator.get_next().await.unwrap().expect("get next should not error");
        assert_eq!(entries.len(), 1);
        let entry = entries.pop().unwrap();
        let diagnostics_data = entry.diagnostics_data.unwrap();
        assert_matches!(diagnostics_data, DiagnosticsData::Socket(_));
        if let DiagnosticsData::Socket(socket) = diagnostics_data {
            let mut socket = AsyncSocket::from_socket(socket).unwrap();
            let mut result = Vec::new();
            let _bytes = socket.read_to_end(&mut result).await.unwrap();
            let data: LogsData = serde_json::from_slice(&result).unwrap();
            let expected_data = make_long_log(1, LONG_LOG_LEN);
            assert_eq!(data, expected_data);
        }

        let empty_entries = iterator.get_next().await.unwrap().expect("get next should not error");
        assert!(empty_entries.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_long_inspect_snapshot() {
        let proxy = setup_diagnostics_bridge_proxy(|p| async {
            FakeArchiveReaderManager::new_with_data(p, vec![], vec![make_long_inspect(1)], vec![])
        });
        let (iterator, server) = create_proxy::<ArchiveIteratorMarker>().unwrap();

        proxy
            .stream_diagnostics(
                BridgeStreamParameters {
                    data_type: Some(DataType::Inspect),
                    stream_mode: Some(StreamMode::Snapshot),
                    ..BridgeStreamParameters::EMPTY
                },
                server,
            )
            .await
            .unwrap()
            .unwrap();

        let mut entries = iterator.get_next().await.unwrap().expect("get next should not error");

        assert_eq!(entries.len(), 1);
        let entry = entries.pop().unwrap();
        let diagnostics_data = entry.diagnostics_data.unwrap();
        assert_matches!(diagnostics_data, DiagnosticsData::Socket(_));
        if let DiagnosticsData::Socket(socket) = diagnostics_data {
            let mut socket = AsyncSocket::from_socket(socket).unwrap();
            let mut result = Vec::new();
            let _bytes = socket.read_to_end(&mut result).await.unwrap();
            let actual: Vec<InspectData> = serde_json::from_slice(&result).unwrap();
            assert_eq!(actual.get(0).unwrap(), &make_long_inspect(1));
        }

        let empty_entries = iterator.get_next().await.unwrap().expect("get next should not error");
        assert!(empty_entries.is_empty());
    }
}
