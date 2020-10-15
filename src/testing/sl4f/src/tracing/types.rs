// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::default::Default;

use serde::{Deserialize, Serialize};

/// Enum for supported Tracing commands.
pub enum TracingMethod {
    Initialize,
    Start,
    Stop,
    Terminate,
}

#[derive(Deserialize)]
pub struct InitializeRequest {
    /// Categories to trace.
    ///
    /// Possible values:
    /// * Omitted (None) for default categories
    /// * Explicit empty list for all categories
    /// * Explicit list of categories to trace
    pub categories: Option<Vec<String>>,
    /// Buffer size to use in MB
    ///
    /// Note: This size is only a suggestion to trace_manager. In particular, very large values
    /// will likely not be respected.
    pub buffer_size: Option<u32>,
}

/// What to do with trace data
#[derive(Deserialize)]
pub enum ResultsDestination {
    /// Completely ignore trace data. Generally used to clean up a trace session left behind by
    /// another trace controller.
    Ignore,
    /// Have trace manager write out the trace data, have the facade read it and return it in the
    /// response to the terminate request.
    WriteAndReturn,
}

impl Default for ResultsDestination {
    fn default() -> Self {
        ResultsDestination::WriteAndReturn
    }
}

#[derive(Deserialize)]
pub struct TerminateRequest {
    /// Whether to download results from the trace. Defaults to ResultsDestination::WriteAndReturn
    ///
    /// Currently, the main use for ResultsDestination::Ignore to clean up a trace session left
    /// behind by another trace controller.
    #[serde(default)]
    pub results_destination: ResultsDestination,
}

#[derive(Serialize)]
pub struct TerminateResponse {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub data: Option<String>,
}

impl std::str::FromStr for TracingMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "Initialize" => Ok(TracingMethod::Initialize),
            "Start" => Ok(TracingMethod::Start),
            "Stop" => Ok(TracingMethod::Stop),
            "Terminate" => Ok(TracingMethod::Terminate),
            _ => return Err(format_err!("invalid Traceutil Facade method: {}", method)),
        }
    }
}
