// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Errors related to starting the component.
///
/// This includes configuration and framework-level errors.
#[derive(thiserror::Error, Debug, PartialEq)]
pub enum ComponentError {
    #[error("key {0} is required to be non-empty")]
    MissingRequiredKey(&'static str),

    #[error("key {0} is not allowed in the program specification")]
    UnknownKey(String),

    #[error("key {0} has the wrong type, expected {1}")]
    WrongType(&'static str, &'static str),

    #[error("accessor must be ALL, FEEDBACK, or LEGACY. Found {0}")]
    UnknownAccessorValue(String),

    #[error("timeout_seconds must be a positive number")]
    InvalidTimeout,

    #[error("test case `{value}` is not valid: {reason}")]
    InvalidTestCase { value: String, reason: String },

    #[error("no outgoing directory channel was provided in component start info")]
    MissingOutgoingChannel,

    #[error(
        "failed to parse generated config using libtriage: {message}\nGenerated Config:\n{config}"
    )]
    TriageConfigError { message: String, config: String },
}

#[derive(thiserror::Error, Debug, PartialEq)]
pub enum EvaluationError {
    #[error("failed to parse Inspect data: {message}\nData:\n{data}")]
    ParseFailure { message: String, data: String },

    #[error("internal failure processing rules: {0}")]
    InternalFailure(String),

    #[error("test case failed!\nFailure reasons:\n{reasons}\nData:\n{data}\nConfig:\n{config}")]
    Failure { reasons: String, data: String, config: String },
}
