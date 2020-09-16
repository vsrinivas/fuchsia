// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics, inspect, lifecycle, logs::server::LogServer,
        repository::DiagnosticsDataRepository, server::DiagnosticsServer,
    },
    anyhow::{bail, format_err, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_diagnostics::{
        self, ArchiveAccessorRequest, ArchiveAccessorRequestStream, BatchIteratorMarker,
        ClientSelectorConfiguration, DataType, Format, Selector, SelectorArgument, StreamMode,
    },
    fuchsia_async::{self as fasync},
    fuchsia_inspect::NumericProperty,
    fuchsia_zircon_status::Status as ZxStatus,
    futures::TryStreamExt,
    log::{error, warn},
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
) -> Result<Vec<Selector>, Error> {
    if selector_args.is_empty() {
        bail!("Empty selectors are a misconfiguration.");
    }
    selector_args
        .into_iter()
        .map(|selector_arg| match selector_arg {
            SelectorArgument::StructuredSelector(x) => match selectors::validate_selector(&x) {
                Ok(_) => Ok(x),
                Err(e) => Err(format_err!("Error validating selector for inspect reading: {}", e)),
            },
            SelectorArgument::RawSelector(x) => selectors::parse_selector(&x).map_err(|e| {
                format_err!("Error parsing selector string for inspect reading: {}", e)
            }),
            _ => Err(format_err!("Unrecognized SelectorArgument type")),
        })
        .collect()
}

macro_rules! close_stream {
    ($results:expr, $epitaph:expr, $($tok:tt)+) => {{
        warn!($($tok)+);
        $results.close_with_epitaph($epitaph).unwrap_or_else(|e| {
            error!("Unable to write epitaph to result stream: {:?}", e)
        });
        return;
    }};
}

macro_rules! ok_or_close {
    ($e:expr, $results:ident) => {
        match $e {
            Ok(v) => v,
            Err(e) => {
                close_stream!(
                    $results,
                    ZxStatus::INVALID_ARGS,
                    concat!(stringify!($e), " was {:?}, returning"),
                    e
                );
            }
        }
    };
}

macro_rules! some_or_close {
    ($e:expr, $results:ident) => {
        match $e {
            Some(v) => v,
            None => {
                close_stream!(
                    $results,
                    ZxStatus::INVALID_ARGS,
                    concat!(stringify!($e), " was None, returning")
                );
            }
        }
    };
}

macro_rules! matches_or_close {
    ($lhs:expr, $rhs:pat, $results:ident) => {
        if !matches!($lhs, $rhs) {
            close_stream!(
                $results,
                ZxStatus::INVALID_ARGS,
                concat!(stringify!($lhs), "={:?} did not match ", stringify!($rhs), ", returning"),
                $lhs,
            );
        }
    };
}

impl ArchiveAccessor {
    /// Create a new accessor for interacting with the archivist's data. The inspect_repo
    /// parameter determines which static configurations scope/restrict the visibility of inspect
    /// data accessed by readers spawned by this accessor.
    pub fn new(
        diagnostics_repo: Arc<RwLock<DiagnosticsDataRepository>>,
        archive_accessor_stats: Arc<diagnostics::ArchiveAccessorStats>,
    ) -> Self {
        ArchiveAccessor { diagnostics_repo, archive_accessor_stats }
    }

    fn handle_stream_inspect(
        &self,
        results: ServerEnd<BatchIteratorMarker>,
        stream_parameters: fidl_fuchsia_diagnostics::StreamParameters,
    ) {
        let inspect_reader_server_stats = Arc::new(
            diagnostics::DiagnosticsServerStats::for_inspect(self.archive_accessor_stats.clone()),
        );

        let format = some_or_close!(stream_parameters.format, results);
        let selector_config =
            some_or_close!(stream_parameters.client_selector_configuration, results);
        let stream_mode = some_or_close!(stream_parameters.stream_mode, results);
        matches_or_close!(stream_mode, StreamMode::Snapshot, results);

        match selector_config {
            ClientSelectorConfiguration::Selectors(selectors) => {
                let selectors =
                    ok_or_close!(validate_and_parse_inspect_selectors(selectors), results);
                let inspect_reader_server = inspect::ReaderServer::new(
                    self.diagnostics_repo.clone(),
                    format,
                    Some(selectors),
                    inspect_reader_server_stats.clone(),
                );

                inspect_reader_server.spawn(results).detach();
            }
            ClientSelectorConfiguration::SelectAll(_) => {
                let inspect_reader_server = inspect::ReaderServer::new(
                    self.diagnostics_repo.clone(),
                    format,
                    None,
                    inspect_reader_server_stats.clone(),
                );

                inspect_reader_server.spawn(results).detach();
            }
            _ => {
                close_stream!(
                    results,
                    ZxStatus::INVALID_ARGS,
                    "Unrecognized selector configuration."
                );
            }
        }
    }

    fn handle_stream_lifecycle_events(
        &self,
        results: ServerEnd<BatchIteratorMarker>,
        stream_parameters: fidl_fuchsia_diagnostics::StreamParameters,
    ) {
        let lifecycle_stats = Arc::new(diagnostics::DiagnosticsServerStats::for_lifecycle(
            self.archive_accessor_stats.clone(),
        ));

        let format = some_or_close!(stream_parameters.format, results);
        matches_or_close!(format, Format::Json, results);
        let selector_config =
            some_or_close!(stream_parameters.client_selector_configuration, results);
        let stream_mode = some_or_close!(stream_parameters.stream_mode, results);
        matches_or_close!(stream_mode, StreamMode::Snapshot, results);

        match selector_config {
            ClientSelectorConfiguration::SelectAll(_) => {
                lifecycle::LifecycleServer::new(
                    self.diagnostics_repo.clone(),
                    None,
                    lifecycle_stats.clone(),
                )
                .spawn(results)
                .detach();
            }
            ClientSelectorConfiguration::Selectors(_) => {
                close_stream!(
                    results,
                    ZxStatus::WRONG_TYPE,
                    "Selectors are not yet supported for lifecycle events."
                );
            }
            _ => close_stream!(results, ZxStatus::INVALID_ARGS, "Unrecognized selectors option."),
        }
    }

    fn handle_stream_logs(
        &self,
        results: ServerEnd<BatchIteratorMarker>,
        stream_parameters: fidl_fuchsia_diagnostics::StreamParameters,
    ) {
        let manager = self.diagnostics_repo.read().log_manager();
        let logs_stats = Arc::new(diagnostics::DiagnosticsServerStats::for_logs(
            self.archive_accessor_stats.clone(),
        ));

        let stream_mode = some_or_close!(stream_parameters.stream_mode, results);
        let format = some_or_close!(stream_parameters.format, results);

        let server =
            ok_or_close!(LogServer::new(manager, stream_mode, format, logs_stats), results);
        server.spawn(results).detach();
    }

    /// Spawn an instance `fidl_fuchsia_diagnostics/Archive` that allows clients to open
    /// reader session to diagnostics data.
    pub fn spawn_archive_accessor_server(self, mut stream: ArchiveAccessorRequestStream) {
        // Self isn't guaranteed to live into the exception handling of the async block. We need to clone self
        // to have a version that can be referenced in the exception handling.
        fasync::Task::spawn(async move {
            self.archive_accessor_stats.global_stats.archive_accessor_connections_opened.add(1);
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    ArchiveAccessorRequest::StreamDiagnostics {
                        result_stream,
                        stream_parameters,
                        control_handle: _,
                    } => {
                        self.archive_accessor_stats.global_stats.stream_diagnostics_requests.add(1);
                        match stream_parameters.data_type {
                            Some(DataType::Inspect) => {
                                self.handle_stream_inspect(result_stream, stream_parameters)
                            }
                            Some(DataType::Lifecycle) => self
                                .handle_stream_lifecycle_events(result_stream, stream_parameters),
                            Some(DataType::Logs) => {
                                self.handle_stream_logs(result_stream, stream_parameters);
                            }
                            None => {
                                close_stream!(
                                    result_stream,
                                    ZxStatus::INVALID_ARGS,
                                    "Client failed to specify a valid data type."
                                );
                            }
                        }
                    }
                }
            }
            self.archive_accessor_stats.global_stats.archive_accessor_connections_closed.add(1);
        })
        .detach();
    }
}
