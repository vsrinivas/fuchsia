// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        constants::{self, FORMATTED_CONTENT_CHUNK_SIZE_TARGET},
        diagnostics::{AccessorStats, ConnectionStats},
        error::AccessorError,
        formatter::{new_batcher, FormattedStream, JsonPacketSerializer, JsonString},
        inspect,
        lifecycle::LifecycleServer,
        moniker_rewriter::MonikerRewriter,
        pipeline::Pipeline,
    },
    anyhow::format_err,
    diagnostics_data::{Data, DiagnosticsData},
    fidl_fuchsia_diagnostics::{
        self, ArchiveAccessorRequest, ArchiveAccessorRequestStream, BatchIteratorRequest,
        BatchIteratorRequestStream, ClientSelectorConfiguration, DataType, Format,
        PerformanceConfiguration, Selector, SelectorArgument, StreamMode, StreamParameters,
    },
    fuchsia_async::{self as fasync, Task},
    fuchsia_inspect::NumericProperty,
    fuchsia_zircon as zx,
    futures::{channel::mpsc::UnboundedSender, prelude::*},
    parking_lot::RwLock,
    selectors,
    serde::Serialize,
    std::collections::HashMap,
    std::convert::{TryFrom, TryInto},
    std::sync::{Arc, Mutex},
    tracing::warn,
};

/// ArchiveAccessor represents an incoming connection from a client to an Archivist
/// instance, through which the client may make Reader requests to the various data
/// sources the Archivist offers.
pub struct ArchiveAccessor {
    // The inspect repository containing read-only inspect data shared across
    // all inspect reader instances.
    pipeline: Arc<RwLock<Pipeline>>,
    archive_accessor_stats: Arc<AccessorStats>,
    moniker_rewriter: Option<Arc<MonikerRewriter>>,
}

fn validate_and_parse_inspect_selectors(
    selector_args: Vec<SelectorArgument>,
) -> Result<Vec<Selector>, AccessorError> {
    let mut selectors = vec![];
    if selector_args.is_empty() {
        Err(AccessorError::EmptySelectors)?;
    }

    for selector_arg in selector_args {
        let selector = match selector_arg {
            SelectorArgument::StructuredSelector(s) => selectors::validate_selector(&s).map(|_| s),
            SelectorArgument::RawSelector(r) => selectors::parse_selector(&r),
            _ => Err(format_err!("unrecognized selector configuration")),
        }
        .map_err(AccessorError::ParseSelectors)?;

        selectors.push(selector);
    }

    Ok(selectors)
}

impl ArchiveAccessor {
    /// Create a new accessor for interacting with the archivist's data. The pipeline
    /// parameter determines which static configurations scope/restrict the visibility of
    /// data accessed by readers spawned by this accessor.
    pub fn new(
        pipeline: Arc<RwLock<Pipeline>>,
        archive_accessor_stats: Arc<AccessorStats>,
    ) -> Self {
        ArchiveAccessor { pipeline, archive_accessor_stats, moniker_rewriter: None }
    }

    pub fn add_moniker_rewriter(mut self, rewriter: Arc<MonikerRewriter>) -> Self {
        self.moniker_rewriter = Some(rewriter);
        self
    }

    async fn run_server(
        pipeline: Arc<RwLock<Pipeline>>,
        requests: BatchIteratorRequestStream,
        params: StreamParameters,
        rewriter: Option<Arc<MonikerRewriter>>,
        accessor_stats: Arc<AccessorStats>,
    ) -> Result<(), AccessorError> {
        let format = params.format.ok_or(AccessorError::MissingFormat)?;
        if !matches!(format, Format::Json) {
            return Err(AccessorError::UnsupportedFormat);
        }
        let mode = params.stream_mode.ok_or(AccessorError::MissingMode)?;

        let performance_config: PerformanceConfig = (&params).try_into()?;

        match params.data_type.ok_or(AccessorError::MissingDataType)? {
            DataType::Inspect => {
                if !matches!(mode, StreamMode::Snapshot) {
                    return Err(AccessorError::UnsupportedMode);
                }
                let stats = Arc::new(ConnectionStats::for_inspect(accessor_stats));

                let selectors =
                    params.client_selector_configuration.ok_or(AccessorError::MissingSelectors)?;

                let selectors = match selectors {
                    ClientSelectorConfiguration::Selectors(selectors) => {
                        Some(validate_and_parse_inspect_selectors(selectors)?)
                    }
                    ClientSelectorConfiguration::SelectAll(_) => None,
                    _ => Err(AccessorError::InvalidSelectors("unrecognized selectors"))?,
                };

                let (selectors, output_rewriter) = match (selectors, rewriter) {
                    (Some(selectors), Some(rewriter)) => rewriter.rewrite_selectors(selectors),
                    // behaves correctly whether selectors is Some(_) or None
                    (selectors, _) => (selectors, None),
                };

                let selectors = selectors.map(|s| s.into_iter().map(Arc::new).collect());

                let unpopulated_container_vec = pipeline.read().fetch_inspect_data(&selectors);

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
                    ),
                    requests,
                    mode,
                    stats,
                    per_component_budget_opt,
                )?
                .run()
                .await
            }
            DataType::Lifecycle => {
                // TODO(fxbug.dev/61350) support other modes
                if !matches!(mode, StreamMode::Snapshot) {
                    return Err(AccessorError::UnsupportedMode);
                }
                let stats = Arc::new(ConnectionStats::for_lifecycle(accessor_stats));

                let selectors =
                    params.client_selector_configuration.ok_or(AccessorError::MissingSelectors)?;
                if !matches!(selectors, ClientSelectorConfiguration::SelectAll(_)) {
                    Err(AccessorError::InvalidSelectors(
                        "lifecycle only supports SelectAll at the moment",
                    ))?;
                }

                let events = LifecycleServer::new(pipeline);

                BatchIterator::new(events, requests, mode, stats, None)?.run().await
            }
            DataType::Logs => {
                let stats = Arc::new(ConnectionStats::for_logs(accessor_stats));
                let logs = pipeline.read().logs(mode);
                BatchIterator::new_serving_arrays(logs, requests, mode, stats)?.run().await
            }
        }
    }

    /// Spawn an instance `fidl_fuchsia_diagnostics/Archive` that allows clients to open
    /// reader session to diagnostics data.
    pub fn spawn_archive_accessor_server(
        self,
        mut stream: ArchiveAccessorRequestStream,
        task_sender: UnboundedSender<Task<()>>,
    ) {
        // Self isn't guaranteed to live into the exception handling of the async block. We need to clone self
        // to have a version that can be referenced in the exception handling.
        let batch_iterator_task_sender = task_sender.clone();
        task_sender
            .unbounded_send(fasync::Task::spawn(async move {
                self.archive_accessor_stats.global_stats.archive_accessor_connections_opened.add(1);
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
                    let moniker_rewriter = self.moniker_rewriter.clone();
                    // Store the batch iterator task so that we can ensure that the client finishes
                    // draining items through it when a Controller#Stop call happens. For example,
                    // this allows tests to fetch all isolated logs before finishing.
                    batch_iterator_task_sender
                        .unbounded_send(Task::spawn(async move {
                            if let Err(e) = Self::run_server(
                                pipeline,
                                requests,
                                stream_parameters,
                                moniker_rewriter,
                                accessor_stats,
                            )
                            .await
                            {
                                e.close(control);
                            }
                        }))
                        .ok();
                }
                self.archive_accessor_stats.global_stats.archive_accessor_connections_closed.add(1);
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
    requests: BatchIteratorRequestStream,
    stats: Arc<ConnectionStats>,
    data: FormattedStream,
    truncation_counter: Option<Arc<Mutex<SchemaTruncationCounter>>>,
}

// Checks if a given schema is within a components budget, and if it is, updates the budget,
// then returns true. Otherwise, if the schema is not within budget, returns false.
fn maybe_update_budget(
    budget_map: &mut HashMap<String, usize>,
    moniker: &String,
    bytes: usize,
    byte_limit: usize,
) -> bool {
    let remaining_budget = budget_map.entry(moniker.to_string()).or_insert(0);
    if *remaining_budget + bytes > byte_limit {
        false
    } else {
        *remaining_budget += bytes;
        true
    }
}

impl BatchIterator {
    pub fn new<Items, D>(
        data: Items,
        requests: BatchIteratorRequestStream,
        mode: StreamMode,
        stats: Arc<ConnectionStats>,
        per_component_byte_limit_opt: Option<usize>,
    ) -> Result<Self, AccessorError>
    where
        Items: Stream<Item = Data<D>> + Send + 'static,
        D: DiagnosticsData,
    {
        let result_stats = stats.clone();

        let mut budget_tracker: HashMap<String, usize> = HashMap::new();

        let truncation_counter = SchemaTruncationCounter::new();
        let stream_owned_counter = truncation_counter.clone();

        let data = data.map(move |d| {
            let mut unlocked_counter = stream_owned_counter.lock().unwrap();
            unlocked_counter.total_schemas += 1;
            if D::has_errors(&d.metadata) {
                result_stats.add_result_error();
            }

            match JsonString::serialize(&d) {
                Err(e) => {
                    result_stats.add_result_error();
                    Err(e)
                }
                Ok(contents) => {
                    result_stats.add_result();
                    match per_component_byte_limit_opt {
                        Some(x) => {
                            if maybe_update_budget(
                                &mut budget_tracker,
                                &d.moniker,
                                contents.len(),
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
                                JsonString::serialize(&new_data)
                            }
                        }
                        None => Ok(contents),
                    }
                }
            }
        });

        Self::new_inner(
            new_batcher(data, stats.clone(), mode),
            requests,
            stats,
            Some(truncation_counter),
        )
    }

    pub fn new_serving_arrays<D, S>(
        data: S,
        requests: BatchIteratorRequestStream,
        mode: StreamMode,
        stats: Arc<ConnectionStats>,
    ) -> Result<Self, AccessorError>
    where
        D: Serialize,
        S: Stream<Item = D> + Send + Unpin + 'static,
    {
        let data =
            JsonPacketSerializer::new(stats.clone(), FORMATTED_CONTENT_CHUNK_SIZE_TARGET, data);
        Self::new_inner(new_batcher(data, stats.clone(), mode), requests, stats, None)
    }

    fn new_inner(
        data: FormattedStream,
        requests: BatchIteratorRequestStream,
        stats: Arc<ConnectionStats>,
        truncation_counter: Option<Arc<Mutex<SchemaTruncationCounter>>>,
    ) -> Result<Self, AccessorError> {
        stats.open_connection();
        Ok(Self { data, requests, stats, truncation_counter })
    }

    pub async fn run(mut self) -> Result<(), AccessorError> {
        while let Some(res) = self.requests.next().await {
            let BatchIteratorRequest::GetNext { responder } = res?;
            self.stats.add_request();
            let start_time = zx::Time::get_monotonic();
            // if we get None back, treat that as a terminal batch with an empty vec
            let batch = self.data.next().await.unwrap_or(vec![]);
            // turn errors into epitaphs -- we drop intermediate items if there was an error midway
            let batch = batch.into_iter().collect::<Result<Vec<_>, _>>()?;

            // increment counters
            self.stats.add_response();
            if batch.is_empty() {
                if let Some(truncation_count) = &self.truncation_counter {
                    let unlocked_count = truncation_count.lock().unwrap();
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
            } => max_aggregate_content_size_bytes.clone(),
            _ => None,
        };

        Ok(PerformanceConfig {
            batch_timeout_sec: batch_timeout_sec_opt
                .unwrap_or(constants::PER_COMPONENT_ASYNC_TIMEOUT_SECONDS),
            aggregated_content_limit_bytes,
        })
    }
}
