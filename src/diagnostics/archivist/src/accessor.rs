// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics::{self, ArchiveAccessorStats, DiagnosticsServerStats},
        inspect,
        lifecycle::LifecycleServer,
        repository::DiagnosticsDataRepository,
        server::{AccessorServer, ServerError},
    },
    anyhow::format_err,
    fidl_fuchsia_diagnostics::{
        self, ArchiveAccessorRequest, ArchiveAccessorRequestStream, BatchIteratorRequestStream,
        ClientSelectorConfiguration, DataType, Format, Selector, SelectorArgument, StreamMode,
    },
    fuchsia_async::{self as fasync, Task},
    fuchsia_inspect::NumericProperty,
    futures::prelude::*,
    log::warn,
    parking_lot::RwLock,
    selectors,
    std::sync::Arc,
};

/// ArchiveAccessor represents an incoming connection from a client to an Archivist
/// instance, through which the client may make Reader requests to the various data
/// sources the Archivist offers.
pub struct ArchiveAccessor {
    // The inspect repository containing read-only inspect data shared across
    // all inspect reader instances.
    diagnostics_repo: Arc<RwLock<DiagnosticsDataRepository>>,
    archive_accessor_stats: Arc<diagnostics::ArchiveAccessorStats>,
}

fn validate_and_parse_inspect_selectors(
    selector_args: Vec<SelectorArgument>,
) -> Result<Vec<Selector>, ServerError> {
    let mut selectors = vec![];
    if selector_args.is_empty() {
        Err(ServerError::EmptySelectors)?;
    }

    for selector_arg in selector_args {
        let selector = match selector_arg {
            SelectorArgument::StructuredSelector(s) => selectors::validate_selector(&s).map(|_| s),
            SelectorArgument::RawSelector(r) => selectors::parse_selector(&r),
            _ => Err(format_err!("unrecognized selector configuration")),
        }
        .map_err(ServerError::ParseSelectors)?;

        selectors.push(selector);
    }

    Ok(selectors)
}

impl ArchiveAccessor {
    /// Create a new accessor for interacting with the archivist's data. The inspect_repo
    /// parameter determines which static configurations scope/restrict the visibility of inspect
    /// data accessed by readers spawned by this accessor.
    pub fn new(
        diagnostics_repo: Arc<RwLock<DiagnosticsDataRepository>>,
        archive_accessor_stats: Arc<ArchiveAccessorStats>,
    ) -> Self {
        ArchiveAccessor { diagnostics_repo, archive_accessor_stats }
    }

    async fn run_server(
        diagnostics_repo: Arc<RwLock<DiagnosticsDataRepository>>,
        requests: BatchIteratorRequestStream,
        params: fidl_fuchsia_diagnostics::StreamParameters,
        accessor_stats: Arc<ArchiveAccessorStats>,
    ) -> Result<(), ServerError> {
        let format = params.format.ok_or(ServerError::MissingFormat)?;
        if !matches!(format, Format::Json) {
            return Err(ServerError::UnsupportedFormat);
        }
        let mode = params.stream_mode.ok_or(ServerError::MissingMode)?;
        if !matches!(mode, StreamMode::Snapshot) {
            return Err(ServerError::UnsupportedMode);
        }

        match params.data_type.ok_or(ServerError::MissingDataType)? {
            DataType::Inspect => {
                let stats = Arc::new(DiagnosticsServerStats::for_inspect(accessor_stats));

                let selectors =
                    params.client_selector_configuration.ok_or(ServerError::MissingSelectors)?;
                let selectors = match selectors {
                    ClientSelectorConfiguration::Selectors(selectors) => {
                        Some(validate_and_parse_inspect_selectors(selectors)?)
                    }
                    ClientSelectorConfiguration::SelectAll(_) => None,
                    _ => Err(ServerError::InvalidSelectors("unrecognized selectors"))?,
                };

                let server = inspect::ReaderServer::new(
                    diagnostics_repo,
                    params.batch_retrieval_timeout_seconds,
                    selectors,
                    stats.clone(),
                );

                server.serve(mode, requests).await
            }
            DataType::Lifecycle => {
                let stats = Arc::new(DiagnosticsServerStats::for_lifecycle(accessor_stats));

                let selectors =
                    params.client_selector_configuration.ok_or(ServerError::MissingSelectors)?;
                if !matches!(selectors, ClientSelectorConfiguration::SelectAll(_)) {
                    Err(ServerError::InvalidSelectors(
                        "lifecycle only supports SelectAll at the moment",
                    ))?;
                }

                let events = LifecycleServer::new(diagnostics_repo);

                AccessorServer::new(events, requests, stats)?.run().await
            }
            DataType::Logs => {
                let stats = Arc::new(DiagnosticsServerStats::for_logs(accessor_stats));
                let logs = diagnostics_repo.read().log_manager();

                AccessorServer::new_serving_arrays(logs.snapshot().await, requests, stats)?
                    .run()
                    .await
            }
        }
    }

    /// Spawn an instance `fidl_fuchsia_diagnostics/Archive` that allows clients to open
    /// reader session to diagnostics data.
    pub fn spawn_archive_accessor_server(self, mut stream: ArchiveAccessorRequestStream) {
        // Self isn't guaranteed to live into the exception handling of the async block. We need to clone self
        // to have a version that can be referenced in the exception handling.
        fasync::Task::spawn(async move {
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
                        warn!("Couldn't bind results channel to executor: {:?}", e);
                        continue;
                    }
                };

                self.archive_accessor_stats.global_stats.stream_diagnostics_requests.add(1);
                let repo = self.diagnostics_repo.clone();
                let accessor_stats = self.archive_accessor_stats.clone();
                Task::spawn(async move {
                    if let Err(e) =
                        Self::run_server(repo, requests, stream_parameters, accessor_stats).await
                    {
                        e.close(control);
                    }
                })
                .detach()
            }
            self.archive_accessor_stats.global_stats.archive_accessor_connections_closed.add(1);
        })
        .detach();
    }
}
