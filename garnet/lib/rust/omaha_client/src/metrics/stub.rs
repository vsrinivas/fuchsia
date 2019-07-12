// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::metrics::{Metrics, MetricsReporter};
use failure::Error;
use log::info;

/// A stub implementation of MetricsReporter which only log metrics.
#[derive(Debug)]
pub struct StubMetricsReporter;

impl MetricsReporter for StubMetricsReporter {
    fn report_metrics(&mut self, metrics: Metrics) -> Result<(), Error> {
        info!("Received request to report metrics: {:?}", metrics);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn test_stub_metrics_reporter() {
        let mut stub = StubMetricsReporter;
        let result = stub.report_metrics(Metrics::UpdateCheckResponseTime(Duration::from_secs(2)));
        assert!(result.is_ok(), "{:?}", result);
    }
}
