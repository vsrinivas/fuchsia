// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::act::ActionContext,
    crate::metrics::{MetricState, MetricValue},
    anyhow::{bail, Error},
};

pub(crate) mod act; // Perform appropriate actions.
pub(crate) mod config; // Read the config file(s) for metric and action specs.
pub(crate) mod metrics; // Retrieve and calculate the metrics.
pub(crate) mod plugins; // Plugins for additional analysis.
pub(crate) mod result_format; // Formats the triage results.
pub(crate) mod validate; // Check config - including that metrics/triggers work correctly.

pub use act::{ActionResults, SnapshotTrigger};
pub use config::{ActionTagDirective, DiagnosticData, ParseResult, Source};
pub use result_format::ActionResultFormatter;

/// Analyze all DiagnosticData against loaded configs and generate corresponding ActionResults.
/// Each DiagnosticData will yield a single ActionResults instance.
pub fn analyze(
    diagnostic_data: &Vec<DiagnosticData>,
    parse_result: &ParseResult,
) -> Result<ActionResults, Error> {
    let mut action_context =
        ActionContext::new(&parse_result.metrics, &parse_result.actions, diagnostic_data);
    Ok(action_context.process().clone())
}

pub fn snapshots(data: &Vec<DiagnosticData>, parse_result: &ParseResult) -> Vec<SnapshotTrigger> {
    let evaluator = ActionContext::new(&parse_result.metrics, &parse_result.actions, data);
    evaluator.into_snapshots()
}

pub fn all_selectors(parse: &ParseResult) -> Vec<String> {
    parse.all_selectors()
}

pub fn evaluate_int_math(expression: &str) -> Result<i64, Error> {
    match MetricState::evaluate_math(&config::parse::parse_expression(expression)?) {
        MetricValue::Int(i) => Ok(i),
        MetricValue::Float(f) => match MetricState::safe_float_to_int(f) {
            Some(i) => Ok(i),
            None => bail!("Non-numeric float result {}", f),
        },
        MetricValue::Missing(msg) => bail!("Eval error: {}", msg),
        bad_type => bail!("Non-numeric result: {:?}", bad_type),
    }
}
