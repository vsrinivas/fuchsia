// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::inspect::{self, InspectDataRepository},
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_diagnostics::{
        AccessorError, ArchiveRequest, ArchiveRequestStream, Selector, SelectorArgument,
    },
    fuchsia_async as fasync,
    futures::{TryFutureExt, TryStreamExt},
    selectors,
    std::sync::{Arc, RwLock},
};

/// ArchiveAccessor represents an incoming connection from a client to an Archivist
/// instance, through which the client may make Reader requests to the various data
/// sources the Archivist offers.
pub struct ArchiveAccessor {
    // The inspect repository containing read-only inspect data shared across
    // all inspect reader instances.
    inspect_repo: Arc<RwLock<InspectDataRepository>>,
}

fn validate_and_parse_selectors(
    selector_args: Vec<SelectorArgument>,
) -> Result<Vec<Selector>, AccessorError> {
    selector_args
        .into_iter()
        .map(|selector_arg| match selector_arg {
            SelectorArgument::StructuredSelector(x) => match selectors::validate_selector(&x) {
                Ok(_) => Ok(x),
                Err(e) => {
                    eprintln!("Error validating selector for archive accessor: {}", e);
                    Err(AccessorError::InvalidSelector)
                }
            },
            SelectorArgument::RawSelector(x) => selectors::parse_selector(&x).map_err(|e| {
                eprintln!("Error parsing component selector string for archive accessor: {}", e);
                AccessorError::InvalidSelector
            }),
            _ => Err(AccessorError::InvalidSelector),
        })
        .collect()
}

impl ArchiveAccessor {
    /// Create a new accessor for interacting with the archivist's data. The inspect_repo
    /// parameter determines which static configurations scope/restrict the visibility of inspect
    /// data accessed by readers spawned by this accessor.
    pub fn new(inspect_repo: Arc<RwLock<InspectDataRepository>>) -> Self {
        ArchiveAccessor { inspect_repo: inspect_repo }
    }

    fn handle_read_inspect(
        &self,
        selectors: Vec<fidl_fuchsia_diagnostics::SelectorArgument>,
        inspect_reader: ServerEnd<fidl_fuchsia_diagnostics::ReaderMarker>,
        responder: fidl_fuchsia_diagnostics::ArchiveReadInspectResponder,
    ) -> Result<(), Error> {
        match validate_and_parse_selectors(selectors) {
            Ok(selector_vec) => {
                responder.send(&mut Ok(()))?;
                let inspect_reader_server =
                    inspect::ReaderServer::new(self.inspect_repo.clone(), selector_vec);
                inspect_reader_server.create_inspect_reader(inspect_reader).unwrap_or_else(|_| {
                    eprintln!("Inspect Reader session crashed.");
                });
                Ok(())
            }
            Err(e) => {
                responder.send(&mut Err(e))?;
                Ok(())
            }
        }
    }

    /// Spawn an instance `fidl_fuchsia_diagnostics/Archive` that allows clients to open
    /// reader session to diagnostics data.
    pub fn spawn_archive_accessor_server(self, mut stream: ArchiveRequestStream) {
        fasync::spawn(
            async move {
                while let Some(req) = stream.try_next().await? {
                    match req {
                        ArchiveRequest::ReadInspect { inspect_reader, selectors, responder } => {
                            self.handle_read_inspect(selectors, inspect_reader, responder)?;
                        }
                        ArchiveRequest::ReadLogs { log_reader: _, selectors: _, responder: _ } => {
                            // TODO(41305): Expose log data via unified reader.
                        }
                        ArchiveRequest::ReadLifecycleEvents {
                            lifecycle_reader: _,
                            selectors: _,
                            responder: _,
                        } => {
                            // TODO(41305): Expose lifecycle data via unified reader.
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| {
                eprintln!("couldn't run archive accessor service: {:?}", e)
            }),
        );
    }
}
