// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::act::ActionContext,
    crate::metrics::{metric_value::MetricValue, MetricState},
    anyhow::{bail, Error},
    injectable_time::{MonotonicTime, TimeSource},
    regex::Regex,
};

pub(crate) mod act; // Perform appropriate actions.
pub(crate) mod config; // Read the config file(s) for metric and action specs.
pub(crate) mod metrics; // Retrieve and calculate the metrics.
pub(crate) mod plugins; // Plugins for additional analysis.
pub(crate) mod result_format; // Formats the triage results.
pub(crate) mod validate; // Check config - including that metrics/triggers work correctly.

pub use act::{ActionResults, SnapshotTrigger};
pub use config::{ActionTagDirective, DataFetcher, DiagnosticData, ParseResult, Source};
pub use result_format::ActionResultFormatter;

const DEVICE_UPTIME_KEY: &str = "device.uptime";

fn time_from_snapshot(files: &Vec<DiagnosticData>) -> Option<i64> {
    if let Some(file) = files.iter().find(|file| file.source == Source::Annotations) {
        if let DataFetcher::KeyValue(fetcher) = &file.data {
            if let MetricValue::String(duration) = fetcher.fetch(DEVICE_UPTIME_KEY) {
                let re = Regex::new(r"^(\d+)d(\d+)h(\d+)m(\d+)s$").unwrap();
                if let Some(c) = re.captures(&duration) {
                    let dhms = (c.get(1), c.get(2), c.get(3), c.get(4));
                    if let (Some(d), Some(h), Some(m), Some(s)) = dhms {
                        let dhms = (
                            d.as_str().parse::<i64>(),
                            h.as_str().parse::<i64>(),
                            m.as_str().parse::<i64>(),
                            s.as_str().parse::<i64>(),
                        );
                        if let (Ok(d), Ok(h), Ok(m), Ok(s)) = dhms {
                            return Some(1_000_000_000 * (s + 60 * (m + 60 * (h + 24 * d))));
                        }
                    }
                }
            }
        }
    }
    None
}

/// Analyze all DiagnosticData against loaded configs and generate corresponding ActionResults.
/// Each DiagnosticData will yield a single ActionResults instance.
pub fn analyze(
    diagnostic_data: &Vec<DiagnosticData>,
    parse_result: &ParseResult,
) -> Result<ActionResults, Error> {
    let now = time_from_snapshot(diagnostic_data);
    let mut action_context =
        ActionContext::new(&parse_result.metrics, &parse_result.actions, diagnostic_data, now);
    Ok(action_context.process().clone())
}

// Do not call this from WASM - WASM does not provde a monotonic clock.
pub fn snapshots(data: &Vec<DiagnosticData>, parse_result: &ParseResult) -> Vec<SnapshotTrigger> {
    let now = Some(MonotonicTime::new().now());
    let evaluator = ActionContext::new(&parse_result.metrics, &parse_result.actions, data, now);
    evaluator.into_snapshots()
}

pub fn all_selectors(parse: &ParseResult) -> Vec<String> {
    parse.all_selectors()
}

pub fn evaluate_int_math(expression: &str) -> Result<i64, Error> {
    match MetricState::evaluate_math(expression) {
        MetricValue::Int(i) => Ok(i),
        MetricValue::Float(f) => match MetricState::safe_float_to_int(f) {
            Some(i) => Ok(i),
            None => bail!("Non-numeric float result {}", f),
        },
        MetricValue::Missing(msg) => bail!("Eval error: {}", msg),
        bad_type => bail!("Non-numeric result: {:?}", bad_type),
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn time_parses_correctly() {
        fn file(name: &str, source: Source, contents: &str) -> DiagnosticData {
            DiagnosticData::new(name.to_string(), source, contents.to_string()).unwrap()
        }
        assert_eq!(time_from_snapshot(&vec![]), None);
        // DiagnosticData can't be created with invalid JSON.
        let files = vec![file("foo.json", Source::Annotations, r#"{"a":"b"}"#)];
        assert_eq!(time_from_snapshot(&files), None);
        let files = vec![file("any.name.works", Source::Annotations, r#"{"device.uptime":"b"}"#)];
        assert_eq!(time_from_snapshot(&files), None);
        let files = vec![file("foo.json", Source::Annotations, r#"{"device.uptime":"1h1m1s"}"#)];
        assert_eq!(time_from_snapshot(&files), None);
        let files = vec![file("foo.json", Source::Annotations, r#"{"device.uptime":"1d1h1m"}"#)];
        assert_eq!(time_from_snapshot(&files), None);
        let files = vec![file("foo.json", Source::Annotations, r#"{"device.uptime":"0d0h0m0s"}"#)];
        assert_eq!(time_from_snapshot(&files), Some(0));
        let files = vec![file("a.b", Source::Annotations, r#"{"device.uptime":"2d3h4m5s"}"#)];
        assert_eq!(time_from_snapshot(&files), Some(1_000_000_000 * 183845));
        let files = vec![file("a.b", Source::Annotations, r#"{"device.uptime":"11d13h17m19s"}"#)];
        let seconds = 19 + 17 * 60 + 13 * 3600 + 11 * 3600 * 24;
        assert_eq!(time_from_snapshot(&files), Some(1_000_000_000 * seconds));
        let files = vec![file("", Source::Annotations, r#"{"device.uptime":"3d5h7m11s"}"#)];
        let hours = 5 + 24 * 3;
        let minutes = 7 + 60 * hours;
        let seconds = 11 + 60 * minutes;
        assert_eq!(time_from_snapshot(&files), Some(1_000_000_000 * seconds));
        let files = vec![file("foo.json", Source::Annotations, r#"{"device.uptime":"0d0h0m1s"}"#)];
        assert_eq!(time_from_snapshot(&files), Some(1_000_000_000));
        let files = vec![file("foo.json", Source::Annotations, r#"{"device.uptime":"0d0h1m0s"}"#)];
        assert_eq!(time_from_snapshot(&files), Some(1_000_000_000 * 60));
        let files = vec![file("foo.json", Source::Annotations, r#"{"device.uptime":"0d1h0m0s"}"#)];
        assert_eq!(time_from_snapshot(&files), Some(1_000_000_000 * 60 * 60));
        let files = vec![file("foo.json", Source::Annotations, r#"{"device.uptime":"1d0h0m0s"}"#)];
        assert_eq!(time_from_snapshot(&files), Some(1_000_000_000 * 60 * 60 * 24));
    }
}
