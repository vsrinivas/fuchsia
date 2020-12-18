// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::protocol::request::InstallSource,
    anyhow::Error,
    std::{cell::RefCell, rc::Rc, time::Duration},
};

#[cfg(test)]
mod mock;
#[cfg(test)]
pub use mock::MockMetricsReporter;
mod stub;
pub use stub::StubMetricsReporter;

/// The list of metrics that can be reported.
#[derive(Debug, Eq, PartialEq)]
pub enum Metrics {
    /// Elapsed time from sending an update check to getting a response from Omaha, with a bool to
    /// hold whether that was a success or a failure.
    UpdateCheckResponseTime { response_time: Duration, successful: bool },
    /// Elapsed time from the previous update check to the current update check.
    UpdateCheckInterval { interval: Duration, clock: ClockType, install_source: InstallSource },
    /// Elapsed time from starting an update to having successfully applied it.
    SuccessfulUpdateDuration(Duration),
    /// Elapsed time from first seeing an update to having successfully applied it.
    SuccessfulUpdateFromFirstSeen(Duration),
    /// Elapsed time from starting an update to encountering a failure.
    FailedUpdateDuration(Duration),
    /// Why an update check failed (network, omaha, proxy, etc).
    UpdateCheckFailureReason(UpdateCheckFailureReason),
    /// Number of omaha request attempts until a response within a single update check attempt,
    /// with a bool to hold whether that was a success or a failure.
    RequestsPerCheck { count: u64, successful: bool },
    /// Number of update check attempts to get an update check to succeed.
    AttemptsToSuccessfulCheck(u64),
    /// Number of install attempts to get an update to succeed.
    AttemptsToSuccessfulInstall { count: u64, successful: bool },
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

#[derive(Debug, Eq, PartialEq)]
pub enum ClockType {
    Monotonic,
    Wall,
}

pub trait MetricsReporter {
    fn report_metrics(&mut self, metrics: Metrics) -> Result<(), Error>;
}

impl<T> MetricsReporter for &mut T
where
    T: MetricsReporter,
{
    fn report_metrics(&mut self, metrics: Metrics) -> Result<(), Error> {
        (*self).report_metrics(metrics)
    }
}

impl<T> MetricsReporter for Rc<RefCell<T>>
where
    T: MetricsReporter,
{
    fn report_metrics(&mut self, metrics: Metrics) -> Result<(), Error> {
        self.borrow_mut().report_metrics(metrics)
    }
}
