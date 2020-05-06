// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::act::ActionContext, anyhow::Error};

pub(crate) mod act; // Perform appropriate actions.
pub(crate) mod config; // Read the config file(s) for metric and action specs.
pub(crate) mod metrics; // Retrieve and calculate the metrics.
pub(crate) mod validate; // Check config - including that metrics/triggers work correctly.
pub(crate) mod result_format; // Formats the triage results.

pub use act::ActionResults;
pub use config::{ActionTagDirective, DiagnosticData, ParseResult};
pub use result_format::ActionResultFormatter;

/// Analyze all DiagnosticData against loaded configs and generate corresponding ActionResults.
/// Each DiagnosticData will yield a single ActionResults instance.
pub fn analyze(
    diagnostic_data: &Vec<DiagnosticData>,
    parse_result: &ParseResult,
) -> Result<Vec<ActionResults>, Error> {
    let mut action_contexts: Vec<ActionContext<'_>> = diagnostic_data
        .iter()
        .map(|d| ActionContext::new(&parse_result.metrics, &parse_result.actions, d))
        .collect();

    Ok(action_contexts.iter_mut().map(|c| c.process().clone()).collect())
}
