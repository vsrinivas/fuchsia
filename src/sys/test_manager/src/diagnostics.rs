// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_trait::async_trait,
    diagnostics_bridge::ArchiveReaderManager,
    diagnostics_data::{Data, LogsData},
    diagnostics_reader as reader,
    fidl::endpoints::{RequestStream, ServerEnd},
    fidl_fuchsia_developer_remotecontrol::StreamError,
    fidl_fuchsia_diagnostics::{
        ArchiveAccessorProxy, ArchiveAccessorRequest, ArchiveAccessorRequestStream,
        BatchIteratorMarker, BatchIteratorProxy, BatchIteratorRequest, BatchIteratorRequestStream,
        ClientSelectorConfiguration, DataType, Format, FormattedContent, StreamMode,
        StreamParameters,
    },
    fidl_fuchsia_mem as fmem, fidl_fuchsia_test_manager as ftest_manager, fuchsia_async as fasync,
    fuchsia_zircon as zx,
    futures::{future::Either, stream::FusedStream, FutureExt, TryStreamExt},
    serde_json::{self, Value as JsonValue},
    std::ops::Deref,
    tracing::{error, warn},
};

pub(crate) struct ServeSyslogOutcome {
    /// Task serving any protocols needed to proxy logs. For example, this is populated
    /// when logs are served over overnet using DiagnosticsBridge.
    pub logs_iterator_task: Option<fasync::Task<Result<(), Error>>>,
    /// A task which resolves when Archivist responds to a request. This task
    /// should resolve before tearing down the realm. This is a workaround that
    /// ensures that Archivist isn't torn down before it receives all ArchiveAccessor
    /// requests.
    // TODO(fxbug.dev/105308): Remove this hack once component events are ordered.
    pub archivist_responding_task: fasync::Task<()>,
}

/// Connect to archivist and starting serving syslog.
pub(crate) fn serve_syslog(
    accessor: ArchiveAccessorProxy,
    log_iterator: ftest_manager::LogsIterator,
) -> Result<ServeSyslogOutcome, StreamError> {
    let mut provider = IsolatedLogsProvider::new(accessor);
    let logs_iterator_task = match log_iterator {
        ftest_manager::LogsIterator::Archive(iterator) => {
            Some(fasync::Task::spawn(provider.run_iterator_server(iterator)?))
        }
        ftest_manager::LogsIterator::Batch(iterator) => {
            provider.start_streaming_logs(iterator)?;
            None
        }
        _ => None,
    };
    let archivist_responding_task = fasync::Task::spawn(async move {
        let (proxy, iterator) =
            fidl::endpoints::create_proxy().expect("cannot create batch iterator");
        if let Err(e) =
            provider.start_streaming(iterator, StreamMode::Snapshot, DataType::Inspect, Some(0))
        {
            warn!("Failed to start streaming logs: {:?}", e);
            return;
        }
        // This should always return something immediately, even if there are no logs
        // due to Snapshot.
        match proxy.get_next().await {
            Ok(Ok(_)) => (),
            other => warn!("Error retrieving logs from archivist: {:?}", other),
        }
    });
    Ok(ServeSyslogOutcome { logs_iterator_task, archivist_responding_task })
}

struct IsolatedLogsProvider {
    accessor: ArchiveAccessorProxy,
}

impl IsolatedLogsProvider {
    fn new(accessor: ArchiveAccessorProxy) -> Self {
        Self { accessor }
    }

    fn start_streaming_logs(
        &self,
        iterator: ServerEnd<BatchIteratorMarker>,
    ) -> Result<(), StreamError> {
        self.start_streaming(iterator, StreamMode::SnapshotThenSubscribe, DataType::Logs, None)
    }

    fn start_streaming(
        &self,
        iterator: ServerEnd<BatchIteratorMarker>,
        stream_mode: StreamMode,
        data_type: DataType,
        batch_timeout: Option<i64>,
    ) -> Result<(), StreamError> {
        let stream_parameters = StreamParameters {
            stream_mode: Some(stream_mode),
            data_type: Some(data_type),
            format: Some(Format::Json),
            client_selector_configuration: Some(ClientSelectorConfiguration::SelectAll(true)),
            batch_retrieval_timeout_seconds: batch_timeout,
            ..StreamParameters::EMPTY
        };
        self.accessor.stream_diagnostics(stream_parameters, iterator).map_err(|err| {
            warn!(%err, "Failed to subscribe to isolated logs");
            StreamError::SetupSubscriptionFailed
        })?;
        Ok(())
    }
}

impl Deref for IsolatedLogsProvider {
    type Target = ArchiveAccessorProxy;

    fn deref(&self) -> &Self::Target {
        &self.accessor
    }
}

#[async_trait]
impl ArchiveReaderManager for IsolatedLogsProvider {
    type Error = reader::Error;

    async fn snapshot<D: diagnostics_data::DiagnosticsData + 'static>(
        &self,
    ) -> Result<Vec<Data<D>>, StreamError> {
        unimplemented!("This functionality is not yet needed.");
    }

    fn start_log_stream(
        &mut self,
    ) -> Result<
        Box<dyn FusedStream<Item = Result<LogsData, Self::Error>> + Unpin + Send>,
        StreamError,
    > {
        let (proxy, batch_iterator_server) = fidl::endpoints::create_proxy::<BatchIteratorMarker>()
            .map_err(|err| {
                warn!(%err, "Fidl error while creating proxy");
                StreamError::GenericError
            })?;
        self.start_streaming_logs(batch_iterator_server)?;
        let subscription = reader::Subscription::new(proxy);
        Ok(Box::new(subscription))
    }
}

/// Runs an ArchiveAccessor to which test components connect.
// TODO(fxbug.dev/110027): remove this interposing entirely as well as the mocks-server in
// running_suite.rs. This remains here due to a mistake when writing the ArchiveAccessor CTF test
// that relied on the change of ordering this was introducing. We have rewritten that test to not
// depend on ordering, but we still need to maintain this.
pub async fn run_intermediary_archive_accessor(
    embedded_archive_accessor: ArchiveAccessorProxy,
    mut stream: ArchiveAccessorRequestStream,
) -> Result<(), Error> {
    while let Some(ArchiveAccessorRequest::StreamDiagnostics {
        result_stream,
        stream_parameters,
        control_handle: _,
    }) = stream.try_next().await?
    {
        let (iterator, server_end) = fidl::endpoints::create_proxy::<BatchIteratorMarker>()?;
        embedded_archive_accessor.stream_diagnostics(stream_parameters, server_end)?;

        fasync::Task::spawn(async move {
            interpose_batch_iterator_responses(iterator, result_stream).await.unwrap_or_else(|e| {
                error!("Failed running batch iterator: {:?}", e);
            })
        })
        .detach();
    }
    Ok(())
}

/// Forward BatchIterator#GetNext requests to the actual archivist and remove the `test_root`
/// prefixes from the monikers in the response.
async fn interpose_batch_iterator_responses(
    iterator: BatchIteratorProxy,
    client_server_end: ServerEnd<BatchIteratorMarker>,
) -> Result<(), Error> {
    let request_stream = client_server_end.into_stream()?;
    let (serve_inner, terminated) = request_stream.into_inner();
    let serve_inner_clone = serve_inner.clone();
    let mut channel_closed_fut =
        fasync::OnSignals::new(serve_inner_clone.channel(), zx::Signals::CHANNEL_PEER_CLOSED)
            .fuse();
    let mut request_stream = BatchIteratorRequestStream::from_inner(serve_inner, terminated);

    while let Some(BatchIteratorRequest::GetNext { responder }) = request_stream.try_next().await? {
        let result =
            match futures::future::select(iterator.get_next(), &mut channel_closed_fut).await {
                Either::Left((result, _)) => result?,
                Either::Right(_) => break,
            };
        match result {
            Err(e) => responder.send(&mut Err(e))?,
            Ok(batch) => {
                let batch = batch
                    .into_iter()
                    .map(|f| write_formatted_content(f))
                    .collect::<Result<Vec<_>, _>>()?;
                responder.send(&mut Ok(batch))?;
            }
        }
    }

    Ok(())
}

fn write_formatted_content(content: FormattedContent) -> Result<FormattedContent, Error> {
    match content {
        FormattedContent::Json(data) => {
            let json_value = load_json_value(data)?;
            let buffer = write_json_value(json_value)?;
            Ok(FormattedContent::Json(buffer))
        }
        // This should never be reached as the Archivist is not serving Text at the moment. When it
        // does we can decide how to parse it to scope this, but for now, not scoping.
        data @ FormattedContent::Text(_) => Ok(data),
        other => Ok(other),
    }
}

fn load_json_value(data: fmem::Buffer) -> Result<JsonValue, Error> {
    let mut buf = vec![0; data.size as usize];
    data.vmo.read(&mut buf, 0)?;
    let hierarchy_json = std::str::from_utf8(&buf)?;
    let result = serde_json::from_str(&hierarchy_json)?;
    Ok(result)
}

fn write_json_value(value: JsonValue) -> Result<fmem::Buffer, Error> {
    let content = value.to_string();
    let size = content.len() as u64;
    let vmo = zx::Vmo::create(size)?;
    vmo.write(content.as_bytes(), 0)?;
    Ok(fmem::Buffer { vmo, size })
}

#[cfg(test)]
mod tests {
    use {super::*, futures::FutureExt};

    #[test]
    fn verify_channel_closure_propagated() {
        // This test verifies that when the client of the mock closes it's channel, the channel to
        // Archivist also closes, even when some other future might be in flight. In case the
        // closure is not propagated, the mock may remain alive and keep Archivist alive even
        // though it's not serving a client any more.

        let mut executor = fasync::TestExecutor::new().expect("create executor");

        let (client_proxy, client_server) =
            fidl::endpoints::create_proxy::<BatchIteratorMarker>().unwrap();
        let (interpose_client, interpose_server) =
            fidl::endpoints::create_proxy::<BatchIteratorMarker>().unwrap();

        let interpose_fut = interpose_batch_iterator_responses(interpose_client, client_server);
        // "Archivist" server just waits for channel closed. This simulates GetNext not having
        // new data
        let server_fut =
            fasync::OnSignals::new(&interpose_server, zx::Signals::CHANNEL_PEER_CLOSED);
        let mut client_fut = client_proxy.get_next().boxed();

        let mut join_fut = futures::future::join(interpose_fut, server_fut).boxed();

        // first, poll client and server futs to ensure client request is received.
        assert!(executor.run_until_stalled(&mut client_fut).is_pending());
        assert!(executor.run_until_stalled(&mut join_fut).is_pending());
        assert!(executor.run_until_stalled(&mut client_fut).is_pending());

        // close channel on client side.
        drop(client_fut);
        drop(client_proxy);

        // server futs should now complete.
        assert!(executor.run_until_stalled(&mut join_fut).is_ready());
    }
}
