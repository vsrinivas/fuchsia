// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        constants::{self, FORMATTED_CONTENT_CHUNK_SIZE_TARGET},
        diagnostics::{AccessorStats, BatchIteratorConnectionStats},
        error::AccessorError,
        formatter::{new_batcher, FormattedStream, JsonPacketSerializer, JsonString},
        inspect::{self, repository::InspectRepository},
        logs::repository::LogsRepository,
        pipeline::Pipeline,
        ImmutableString,
    },
    async_lock::{Mutex, RwLock},
    diagnostics_data::{Data, DiagnosticsData},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_diagnostics::{
        self, ArchiveAccessorRequest, ArchiveAccessorRequestStream, BatchIteratorRequest,
        BatchIteratorRequestStream, ClientSelectorConfiguration, DataType, Format,
        PerformanceConfiguration, Selector, SelectorArgument, StreamMode, StreamParameters,
        StringSelector, TreeSelector, TreeSelectorUnknown,
    },
    fuchsia_async::{self as fasync, Task},
    fuchsia_inspect::NumericProperty,
    fuchsia_trace as ftrace, fuchsia_zircon as zx,
    futures::{
        channel::mpsc::UnboundedSender,
        future::{select, Either},
        prelude::*,
    },
    selectors::{self, FastError},
    serde::Serialize,
    std::collections::HashMap,
    std::convert::TryFrom,
    std::sync::Arc,
    tracing::warn,
};

/// ArchiveAccessor represents an incoming connection from a client to an Archivist
/// instance, through which the client may make Reader requests to the various data
/// sources the Archivist offers.
pub struct ArchiveAccessor {
    // The inspect repository containing read-only inspect data shared across
    // all inspect reader instances.
    pipeline: Arc<RwLock<Pipeline>>,
    inspect_repository: Arc<InspectRepository>,
    logs_repository: Arc<LogsRepository>,
    archive_accessor_stats: Arc<AccessorStats>,
}

fn validate_and_parse_selectors(
    selector_args: Vec<SelectorArgument>,
) -> Result<Vec<Selector>, AccessorError> {
    let mut selectors = vec![];
    let mut errors = vec![];

    if selector_args.is_empty() {
        return Err(AccessorError::EmptySelectors);
    }

    for selector_arg in selector_args {
        match selectors::take_from_argument::<FastError>(selector_arg) {
            Ok(s) => selectors.push(s),
            Err(e) => errors.push(e),
        }
    }

    if !errors.is_empty() {
        warn!(?errors, "Found errors in selector arguments");
    }

    Ok(selectors)
}

fn validate_and_parse_log_selectors(
    selector_args: Vec<SelectorArgument>,
) -> Result<Vec<Selector>, AccessorError> {
    // Only accept selectors of the type: `component:root` for logs for now.
    let selectors = validate_and_parse_selectors(selector_args)?;
    for selector in &selectors {
        // Unwrap safe: Previous validation discards any selector without a node.
        let tree_selector = selector.tree_selector.as_ref().unwrap();
        match tree_selector {
            TreeSelector::PropertySelector(_) => {
                return Err(AccessorError::InvalidLogSelector);
            }
            TreeSelector::SubtreeSelector(subtree_selector) => {
                if subtree_selector.node_path.len() != 1 {
                    return Err(AccessorError::InvalidLogSelector);
                }
                match &subtree_selector.node_path[0] {
                    StringSelector::ExactMatch(val) if val == "root" => {}
                    StringSelector::StringPattern(val) if val == "root" => {}
                    _ => {
                        return Err(AccessorError::InvalidLogSelector);
                    }
                }
            }
            TreeSelectorUnknown!() => {}
        }
    }
    Ok(selectors)
}

impl ArchiveAccessor {
    /// Create a new accessor for interacting with the archivist's data. The pipeline
    /// parameter determines which static configurations scope/restrict the visibility of
    /// data accessed by readers spawned by this accessor.
    pub fn new(
        pipeline: Arc<RwLock<Pipeline>>,
        inspect_repository: Arc<InspectRepository>,
        logs_repository: Arc<LogsRepository>,
        archive_accessor_stats: Arc<AccessorStats>,
    ) -> Self {
        ArchiveAccessor { pipeline, archive_accessor_stats, inspect_repository, logs_repository }
    }

    async fn run_server(
        pipeline: Arc<RwLock<Pipeline>>,
        inspect_repo: Arc<InspectRepository>,
        log_repo: Arc<LogsRepository>,
        requests: BatchIteratorRequestStream,
        params: StreamParameters,
        accessor_stats: Arc<AccessorStats>,
    ) -> Result<(), AccessorError> {
        let format = params.format.ok_or(AccessorError::MissingFormat)?;
        if !matches!(format, Format::Json) {
            return Err(AccessorError::UnsupportedFormat);
        }
        let mode = params.stream_mode.ok_or(AccessorError::MissingMode)?;

        let performance_config: PerformanceConfig = PerformanceConfig::try_from(&params)?;

        let trace_id = ftrace::Id::random();
        match params.data_type.ok_or(AccessorError::MissingDataType)? {
            DataType::Inspect => {
                let _trace_guard = ftrace::async_enter!(
                    trace_id,
                    "app",
                    "ArchiveAccessor::run_server",
                    "data_type" => "Inspect",
                    "trace_id" => u64::from(trace_id)
                );
                if !matches!(mode, StreamMode::Snapshot) {
                    return Err(AccessorError::UnsupportedMode);
                }
                let stats = Arc::new(accessor_stats.new_inspect_batch_iterator());

                let selectors =
                    params.client_selector_configuration.ok_or(AccessorError::MissingSelectors)?;

                let selectors = match selectors {
                    ClientSelectorConfiguration::Selectors(selectors) => {
                        Some(validate_and_parse_selectors(selectors)?)
                    }
                    ClientSelectorConfiguration::SelectAll(_) => None,
                    _ => return Err(AccessorError::InvalidSelectors("unrecognized selectors")),
                };

                let (selectors, output_rewriter) =
                    match (selectors, pipeline.read().await.moniker_rewriter().as_ref()) {
                        (Some(selectors), Some(rewriter)) => rewriter.rewrite_selectors(selectors),
                        // behaves correctly whether selectors is Some(_) or None
                        (selectors, _) => (selectors, None),
                    };

                let unpopulated_container_vec = inspect_repo
                    .fetch_inspect_data(
                        &selectors,
                        pipeline.read().await.static_selectors_matchers(),
                    )
                    .await;

                let per_component_budget_opt = if unpopulated_container_vec.is_empty() {
                    None
                } else {
                    performance_config
                        .aggregated_content_limit_bytes
                        .map(|limit| (limit as usize) / unpopulated_container_vec.len())
                };

                if let Some(max_snapshot_size) = performance_config.aggregated_content_limit_bytes {
                    stats.global_stats().record_max_snapshot_size_config(max_snapshot_size);
                }

                BatchIterator::new(
                    inspect::ReaderServer::stream(
                        unpopulated_container_vec,
                        performance_config,
                        selectors,
                        output_rewriter,
                        stats.clone(),
                        trace_id,
                    ),
                    requests,
                    mode,
                    stats,
                    per_component_budget_opt,
                    trace_id,
                )?
                .run()
                .await
            }
            DataType::Logs => {
                let _trace_guard = ftrace::async_enter!(
                    trace_id,
                    "app",
                    "ArchiveAccessor::run_server",
                    "data_type" => "Logs",
                    // An async duration cannot have multiple concurrent child async durations
                    // so we include the nonce as metadata to manually determine relationship.
                    "trace_id" => u64::from(trace_id)
                );
                let stats = Arc::new(accessor_stats.new_logs_batch_iterator());
                let selectors = match params.client_selector_configuration {
                    Some(ClientSelectorConfiguration::Selectors(selectors)) => {
                        Some(validate_and_parse_log_selectors(selectors)?)
                    }
                    Some(ClientSelectorConfiguration::SelectAll(_)) => None,
                    _ => return Err(AccessorError::InvalidSelectors("unrecognized selectors")),
                };
                let logs = log_repo
                    .logs_cursor(mode, selectors, trace_id)
                    .await
                    .map(move |inner: _| (*inner).clone());
                BatchIterator::new_serving_arrays(logs, requests, mode, stats, trace_id)?
                    .run()
                    .await
            }
        }
    }

    /// Spawn an instance `fidl_fuchsia_diagnostics/Archive` that allows clients to open
    /// reader session to diagnostics data.
    pub fn spawn_server(
        self,
        mut stream: ArchiveAccessorRequestStream,
        task_sender: UnboundedSender<Task<()>>,
    ) {
        // Self isn't guaranteed to live into the exception handling of the async block. We need to clone self
        // to have a version that can be referenced in the exception handling.
        let batch_iterator_task_sender = task_sender.clone();
        task_sender
            .unbounded_send(fasync::Task::spawn(async move {
                self.archive_accessor_stats.global_stats.connections_opened.add(1);
                while let Ok(Some(ArchiveAccessorRequest::StreamDiagnostics {
                    result_stream,
                    stream_parameters,
                    control_handle: _,
                })) = stream.try_next().await
                {
                    let (requests, control) = match result_stream.into_stream_and_control_handle() {
                        Ok(r) => r,
                        Err(e) => {
                            warn!(?e, "Couldn't bind results channel to executor.");
                            continue;
                        }
                    };

                    self.archive_accessor_stats.global_stats.stream_diagnostics_requests.add(1);
                    let pipeline = self.pipeline.clone();
                    let accessor_stats = self.archive_accessor_stats.clone();
                    let log_repo = self.logs_repository.clone();
                    let inspect_repo = self.inspect_repository.clone();
                    // Store the batch iterator task so that we can ensure that the client finishes
                    // draining items through it when a Controller#Stop call happens. For example,
                    // this allows tests to fetch all isolated logs before finishing.
                    batch_iterator_task_sender
                        .unbounded_send(Task::spawn(async move {
                            if let Err(e) = Self::run_server(
                                pipeline,
                                inspect_repo,
                                log_repo,
                                requests,
                                stream_parameters,
                                accessor_stats,
                            )
                            .await
                            {
                                e.close(control);
                            }
                        }))
                        .ok();
                }
                self.archive_accessor_stats.global_stats.connections_closed.add(1);
            }))
            .ok();
    }
}

struct SchemaTruncationCounter {
    truncated_schemas: u64,
    total_schemas: u64,
}

impl SchemaTruncationCounter {
    pub fn new() -> Arc<Mutex<Self>> {
        Arc::new(Mutex::new(Self { truncated_schemas: 0, total_schemas: 0 }))
    }
}

pub struct BatchIterator {
    /// requests is always populated on construction and is removed in run().
    /// This is an option as run() needs to consume it, but the Drop impl prevents us
    /// from unpacking BatchIterator.
    requests: Option<BatchIteratorRequestStream>,
    stats: Arc<BatchIteratorConnectionStats>,
    data: FormattedStream,
    truncation_counter: Option<Arc<Mutex<SchemaTruncationCounter>>>,
    parent_trace_id: ftrace::Id,
}

// Checks if a given schema is within a components budget, and if it is, updates the budget,
// then returns true. Otherwise, if the schema is not within budget, returns false.
fn maybe_update_budget(
    budget_map: &mut HashMap<ImmutableString, usize>,
    moniker: &str,
    bytes: usize,
    byte_limit: usize,
) -> bool {
    if let Some(remaining_budget) = budget_map.get_mut(moniker) {
        if *remaining_budget + bytes > byte_limit {
            false
        } else {
            *remaining_budget += bytes;
            true
        }
    } else if bytes > byte_limit {
        budget_map.insert(moniker.to_string().into_boxed_str(), 0);
        false
    } else {
        budget_map.insert(moniker.to_string().into_boxed_str(), bytes);
        true
    }
}

impl BatchIterator {
    pub fn new<Items, D>(
        data: Items,
        requests: BatchIteratorRequestStream,
        mode: StreamMode,
        stats: Arc<BatchIteratorConnectionStats>,
        per_component_byte_limit_opt: Option<usize>,
        parent_trace_id: ftrace::Id,
    ) -> Result<Self, AccessorError>
    where
        Items: Stream<Item = Data<D>> + Send + 'static,
        D: DiagnosticsData + 'static,
    {
        let result_stats_for_fut = stats.clone();

        let budget_tracker_shared = Arc::new(Mutex::new(HashMap::new()));

        let truncation_counter = SchemaTruncationCounter::new();
        let stream_owned_counter_for_fut = truncation_counter.clone();

        let data = data.then(move |d| {
            let stream_owned_counter = stream_owned_counter_for_fut.clone();
            let result_stats = result_stats_for_fut.clone();
            let budget_tracker = budget_tracker_shared.clone();
            async move {
                let trace_id = ftrace::Id::random();
                let _trace_guard = ftrace::async_enter!(
                    trace_id,
                    "app",
                    "BatchIterator::new.serialize",
                    // An async duration cannot have multiple concurrent child async durations
                    // so we include the nonce as metadata to manually determine relationship.
                    "parent_trace_id" => u64::from(parent_trace_id),
                    "trace_id" => u64::from(trace_id),
                    "moniker" => d.moniker.as_ref()
                );
                let mut unlocked_counter = stream_owned_counter.lock().await;
                let mut tracker_guard = budget_tracker.lock().await;
                unlocked_counter.total_schemas += 1;
                if D::has_errors(&d.metadata) {
                    result_stats.add_result_error();
                }

                match JsonString::serialize(&d, D::DATA_TYPE) {
                    Err(e) => {
                        result_stats.add_result_error();
                        Err(e)
                    }
                    Ok(contents) => {
                        result_stats.add_result();
                        match per_component_byte_limit_opt {
                            Some(x) => {
                                if maybe_update_budget(
                                    &mut tracker_guard,
                                    &d.moniker,
                                    contents.size as usize,
                                    x,
                                ) {
                                    Ok(contents)
                                } else {
                                    result_stats.add_schema_truncated();
                                    unlocked_counter.truncated_schemas += 1;

                                    let new_data = d.dropped_payload_schema(
                                        "Schema failed to fit component budget.".to_string(),
                                    );

                                    // TODO(66085): If a payload is truncated, cache the
                                    // new schema so that we can reuse if other schemas from
                                    // the same component get dropped.
                                    JsonString::serialize(&new_data, D::DATA_TYPE)
                                }
                            }
                            None => Ok(contents),
                        }
                    }
                }
            }
        });

        Self::new_inner(
            new_batcher(data, stats.clone(), mode),
            requests,
            stats,
            Some(truncation_counter),
            parent_trace_id,
        )
    }

    pub fn new_serving_arrays<D, S>(
        data: S,
        requests: BatchIteratorRequestStream,
        mode: StreamMode,
        stats: Arc<BatchIteratorConnectionStats>,
        parent_trace_id: ftrace::Id,
    ) -> Result<Self, AccessorError>
    where
        D: Serialize + Send + 'static,
        S: Stream<Item = D> + Send + Unpin + 'static,
    {
        let data =
            JsonPacketSerializer::new(stats.clone(), FORMATTED_CONTENT_CHUNK_SIZE_TARGET, data);
        Self::new_inner(
            new_batcher(data, stats.clone(), mode),
            requests,
            stats,
            None,
            parent_trace_id,
        )
    }

    fn new_inner(
        data: FormattedStream,
        requests: BatchIteratorRequestStream,
        stats: Arc<BatchIteratorConnectionStats>,
        truncation_counter: Option<Arc<Mutex<SchemaTruncationCounter>>>,
        parent_trace_id: ftrace::Id,
    ) -> Result<Self, AccessorError> {
        stats.open_connection();
        Ok(Self { data, requests: Some(requests), stats, truncation_counter, parent_trace_id })
    }

    pub async fn run(mut self) -> Result<(), AccessorError> {
        let (serve_inner, terminated) =
            self.requests.take().expect("request stream should be present").into_inner();
        let serve_inner_clone = serve_inner.clone();
        let channel_closed_fut =
            fasync::OnSignals::new(serve_inner_clone.channel(), zx::Signals::CHANNEL_PEER_CLOSED)
                .shared();
        let mut requests = BatchIteratorRequestStream::from_inner(serve_inner, terminated);

        while let Some(res) = requests.next().await {
            let BatchIteratorRequest::GetNext { responder } = res?;
            self.stats.add_request();
            let start_time = zx::Time::get_monotonic();
            let trace_id = ftrace::Id::random();
            let _trace_guard = ftrace::async_enter!(
                trace_id,
                "app",
                "BatchIterator::run.get_send_batch",
                // An async duration cannot have multiple concurrent child async durations
                // so we include the nonce as metadata to manually determine relationship.
                "parent_trace_id" => u64::from(self.parent_trace_id),
                "trace_id" => u64::from(trace_id)
            );
            let batch = match select(self.data.next(), channel_closed_fut.clone()).await {
                // if we get None back, treat that as a terminal batch with an empty vec
                Either::Left((batch_option, _)) => batch_option.unwrap_or_default(),
                // if the client closes the channel, stop waiting and terminate.
                Either::Right(_) => break,
            };

            // turn errors into epitaphs -- we drop intermediate items if there was an error midway
            let batch = batch.into_iter().collect::<Result<Vec<_>, _>>()?;

            // increment counters
            self.stats.add_response();
            if batch.is_empty() {
                if let Some(truncation_count) = &self.truncation_counter {
                    let unlocked_count = truncation_count.lock().await;
                    if unlocked_count.total_schemas > 0 {
                        self.stats.global_stats().record_percent_truncated_schemas(
                            ((unlocked_count.truncated_schemas as f32
                                / unlocked_count.total_schemas as f32)
                                * 100.0)
                                .round() as u64,
                        );
                    }
                }
                self.stats.add_terminal();
            }
            self.stats.global_stats().record_batch_duration(zx::Time::get_monotonic() - start_time);

            let mut response = Ok(batch);
            responder.send(&mut response)?;
        }
        Ok(())
    }
}

impl Drop for BatchIterator {
    fn drop(&mut self) {
        self.stats.close_connection();
    }
}

pub struct PerformanceConfig {
    pub batch_timeout_sec: i64,
    pub aggregated_content_limit_bytes: Option<u64>,
}

impl TryFrom<&StreamParameters> for PerformanceConfig {
    type Error = AccessorError;
    fn try_from(params: &StreamParameters) -> Result<PerformanceConfig, Self::Error> {
        let batch_timeout_sec_opt = match params {
            // If only nested batch retrieval timeout is definitely not set,
            // use the optional outer field.
            StreamParameters {
                batch_retrieval_timeout_seconds,
                performance_configuration: None,
                ..
            }
            | StreamParameters {
                batch_retrieval_timeout_seconds,
                performance_configuration:
                    Some(PerformanceConfiguration { batch_retrieval_timeout_seconds: None, .. }),
                ..
            } => batch_retrieval_timeout_seconds,
            // If the outer field is definitely not set, and the inner field might be,
            // use the inner field.
            StreamParameters {
                batch_retrieval_timeout_seconds: None,
                performance_configuration:
                    Some(PerformanceConfiguration { batch_retrieval_timeout_seconds, .. }),
                ..
            } => batch_retrieval_timeout_seconds,
            // Both the inner and outer fields are set, which is an error.
            _ => return Err(AccessorError::DuplicateBatchTimeout),
        };

        let aggregated_content_limit_bytes = match params {
            StreamParameters {
                performance_configuration:
                    Some(PerformanceConfiguration { max_aggregate_content_size_bytes, .. }),
                ..
            } => *max_aggregate_content_size_bytes,
            _ => None,
        };

        Ok(PerformanceConfig {
            batch_timeout_sec: batch_timeout_sec_opt
                .unwrap_or(constants::PER_COMPONENT_ASYNC_TIMEOUT_SECONDS),
            aggregated_content_limit_bytes,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{logs::budget::BudgetManager, pipeline::Pipeline};
    use assert_matches::assert_matches;
    use async_lock::RwLock;
    use fidl_fuchsia_diagnostics::{ArchiveAccessorMarker, BatchIteratorMarker};
    use fuchsia_inspect::{Inspector, Node};
    use fuchsia_zircon_status as zx_status;
    use futures::channel::mpsc;

    #[fuchsia::test]
    async fn logs_only_accept_basic_component_selectors() {
        let (accessor, stream) =
            fidl::endpoints::create_proxy_and_stream::<ArchiveAccessorMarker>().unwrap();
        let (snd, _rcv) = mpsc::unbounded();
        fasync::Task::spawn(async move {
            let pipeline = Arc::new(RwLock::new(Pipeline::for_test(None)));
            let inspector = Inspector::new();
            let budget = BudgetManager::new(1_000_000);
            let log_repo = Arc::new(LogsRepository::new(&budget, inspector.root()).await);
            let inspect_repo = Arc::new(InspectRepository::new(vec![Arc::downgrade(&pipeline)]));
            let accessor = ArchiveAccessor::new(
                pipeline,
                inspect_repo,
                log_repo,
                Arc::new(AccessorStats::new(Node::default())),
            );
            accessor.spawn_server(stream, snd);
        })
        .detach();

        // A selector of the form `component:node/path:property` is rejected.
        let (batch_iterator, server_end) =
            fidl::endpoints::create_proxy::<BatchIteratorMarker>().unwrap();
        assert!(accessor
            .r#stream_diagnostics(
                StreamParameters {
                    data_type: Some(DataType::Logs),
                    stream_mode: Some(StreamMode::SnapshotThenSubscribe),
                    format: Some(Format::Json),
                    client_selector_configuration: Some(ClientSelectorConfiguration::Selectors(
                        vec![SelectorArgument::RawSelector("foo:root/bar:baz".to_string()),]
                    )),
                    ..StreamParameters::EMPTY
                },
                server_end
            )
            .is_ok());
        assert_matches!(
            batch_iterator.get_next().await,
            Err(fidl::Error::ClientChannelClosed { status: zx_status::Status::INVALID_ARGS, .. })
        );

        // A selector of the form `component:root` is accepted.
        let (batch_iterator, server_end) =
            fidl::endpoints::create_proxy::<BatchIteratorMarker>().unwrap();
        assert!(accessor
            .r#stream_diagnostics(
                StreamParameters {
                    data_type: Some(DataType::Logs),
                    stream_mode: Some(StreamMode::Snapshot),
                    format: Some(Format::Json),
                    client_selector_configuration: Some(ClientSelectorConfiguration::Selectors(
                        vec![SelectorArgument::RawSelector("foo:root".to_string()),]
                    )),
                    ..StreamParameters::EMPTY
                },
                server_end
            )
            .is_ok());

        assert!(batch_iterator.get_next().await.is_ok());
    }

    #[fuchsia::test]
    async fn accessor_skips_invalid_selectors() {
        let (accessor, stream) =
            fidl::endpoints::create_proxy_and_stream::<ArchiveAccessorMarker>().unwrap();
        let (snd, _rcv) = mpsc::unbounded();
        fasync::Task::spawn(async move {
            let pipeline = Arc::new(RwLock::new(Pipeline::for_test(None)));
            let inspector = Inspector::new();
            let budget = BudgetManager::new(1_000_000);
            let log_repo = Arc::new(LogsRepository::new(&budget, inspector.root()).await);
            let inspect_repo = Arc::new(InspectRepository::new(vec![Arc::downgrade(&pipeline)]));
            let accessor = ArchiveAccessor::new(
                pipeline,
                inspect_repo,
                log_repo,
                Arc::new(AccessorStats::new(Node::default())),
            );
            accessor.spawn_server(stream, snd);
        })
        .detach();

        // A selector of the form `component:node/path:property` is rejected.
        let (batch_iterator, server_end) =
            fidl::endpoints::create_proxy::<BatchIteratorMarker>().unwrap();

        assert!(accessor
            .r#stream_diagnostics(
                StreamParameters {
                    data_type: Some(DataType::Inspect),
                    stream_mode: Some(StreamMode::Snapshot),
                    format: Some(Format::Json),
                    client_selector_configuration: Some(ClientSelectorConfiguration::Selectors(
                        vec![
                            SelectorArgument::RawSelector("invalid".to_string()),
                            SelectorArgument::RawSelector("valid:root".to_string()),
                        ]
                    )),
                    ..StreamParameters::EMPTY
                },
                server_end
            )
            .is_ok());

        // The batch iterator proxy should remain valid and providing responses regardless of the
        // invalid selectors that were given.
        assert!(batch_iterator.get_next().await.is_ok());
    }

    #[fuchsia::test]
    fn batch_iterator_terminates_on_client_disconnect() {
        let mut executor = fasync::TestExecutor::new().expect("create executor");
        let (batch_iterator_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<BatchIteratorMarker>().unwrap();
        // Create a batch iterator that uses a hung stream to serve logs.
        let batch_iterator = BatchIterator::new(
            futures::stream::pending::<diagnostics_data::Data<diagnostics_data::Logs>>(),
            stream,
            StreamMode::Subscribe,
            Arc::new(AccessorStats::new(Node::default()).new_inspect_batch_iterator()),
            None,
            ftrace::Id::random(),
        )
        .expect("create batch iterator");

        let mut batch_iterator_fut = batch_iterator.run().boxed();
        assert!(executor.run_until_stalled(&mut batch_iterator_fut).is_pending());

        // After sending a request, the request should be unfulfilled.
        let mut iterator_request_fut = batch_iterator_proxy.get_next();
        assert!(executor.run_until_stalled(&mut iterator_request_fut).is_pending());
        assert!(executor.run_until_stalled(&mut batch_iterator_fut).is_pending());
        assert!(executor.run_until_stalled(&mut iterator_request_fut).is_pending());

        // After closing the client end of the channel, the server should terminate and release
        // resources.
        drop(iterator_request_fut);
        drop(batch_iterator_proxy);
        assert_matches!(
            executor.run_until_stalled(&mut batch_iterator_fut),
            core::task::Poll::Ready(Ok(()))
        );
    }
}
