// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use crate::events::types::{EventValidationError, SourceIdentityValidationError, ValidatedEvent};
use fidl_fuchsia_sys2::EventResult;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum EventError {
    #[error("missing `{0}`")]
    MissingField(&'static str),

    #[error("incorrect capability name {received} (expected {expected})")]
    IncorrectName { received: String, expected: &'static str },

    #[error("server end from event was invalid: {source}")]
    InvalidServerEnd {
        #[from]
        source: fidl::Error,
    },

    #[error("received a fuchsia.sys2/EventError: {description}")]
    ReceivedError { description: String },

    #[error("received an invalid event type {ty:?}")]
    InvalidEventType { ty: fidl_fuchsia_sys2::EventType },

    #[error("received an unknown event result {unknown:?}")]
    UnknownResult { unknown: fidl_fuchsia_sys2::EventResult },

    #[error("unable to validate fuchsia.sys2/Event: {source}")]
    EventValidationFailed {
        #[from]
        source: EventValidationError,
    },

    #[error("unable to validate fuchsia.sys.internal/SourceIdentity: {source}")]
    SourceIdentityValidationFailed {
        #[from]
        source: SourceIdentityValidationError,
    },

    #[error("missing diagnostics directory in DirectoryReady payload")]
    MissingDiagnosticsDir,

    #[error("running event didn't encode start timestamp.")]
    MissingStartTimestamp,

    #[error("event did not have a payload: {event:?}")]
    MissingPayload { event: ValidatedEvent },

    #[error("unknown result type received: {result:?}")]
    UnrecognizedResult { result: EventResult },

    #[error("attempted to take a stream that has already been taken")]
    StreamAlreadyTaken,

    #[error("failed to retrieve log connection")]
    RetrieveLogConnection(#[source] fidl::Error),

    #[error("FIDL error ({0}): {1:?}")]
    Fidl(&'static str, #[source] fidl::Error),

    #[error("FIDL Component error: {0}")]
    FidlComponent(String),

    #[error("couldn't parse a moniker we received")]
    ParsingMoniker {
        #[from]
        source: MonikerError,
    },
}

#[derive(Debug, Error)]
pub enum MonikerError {
    #[error("couldn't parse `{0}` as a moniker due to incorrect prefix, expected `./`")]
    InvalidMonikerPrefix(String),

    #[error(
        "moniker segment `{0}` couldn't be parsed, \
        expected either COLLECTION:NAME:INSTANCE or NAME:INSTANCE"
    )]
    InvalidSegment(String),
}
