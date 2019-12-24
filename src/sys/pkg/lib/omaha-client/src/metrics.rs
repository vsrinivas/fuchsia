// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use std::time::Duration;

#[cfg(test)]
mod mock;
#[cfg(test)]
pub use mock::MockMetricsReporter;
mod stub;
pub use stub::StubMetricsReporter;

/// The list of metrics that can be reported.
#[derive(Debug, Eq, PartialEq)]
pub enum Metrics {
    /// Elapsed time from sending an update check to getting a response from Omaha.
    UpdateCheckResponseTime(Duration),
    /// Elapsed time from the previous update check to the current update check.
    UpdateCheckInterval(Duration),
    /// Elapsed time from starting an update to having successfully applied it.
    SuccessfulUpdateDuration(Duration),
    /// Elapsed time from first seeing an update to having successfully applied it.
    SuccessfulUpdateFromFirstSeen(Duration),
    /// Elapsed time from starting an update to encountering a failure.
    FailedUpdateDuration(Duration),
    /// Why an update check failed (network, omaha, proxy, etc).
    UpdateCheckFailureReason(UpdateCheckFailureReason),
    /// Number of omaha request attempts within a single update check attempt.
    UpdateCheckRetries(u64),
    /// Number of update check attempts to get an update to succeed.
    AttemptsToSucceed(u64),
    /// Elapsed time from having finished applying the update to when finally
    /// running that software, it is sent after the reboot (and includes the
    /// rebooting time).
    WaitedForRebootDuration(Duration),
    /// Number of time an update failed to boot into new version.
    FailedBootAttempts(u64),
}

#[derive(Debug, Eq, PartialEq)]
pub enum UpdateCheckFailureReason {
    Omaha = 0,
    Network = 1,
    Proxy = 2,
    Configuration = 3,
    Internal = 4,
}

pub trait MetricsReporter {
    fn report_metrics(&mut self, metrics: Metrics) -> Result<(), Error>;
}
