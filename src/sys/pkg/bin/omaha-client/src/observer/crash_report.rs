// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Helpers for triggering best-effort crash reports.

use anyhow::anyhow;
use fidl_fuchsia_feedback::{CrashReport, CrashReporterProxy};
use fuchsia_zircon as zx;
use futures::{channel::mpsc, future::LocalBoxFuture, prelude::*};
use omaha_client::time::TimeSource;
use std::time::Duration;
use tracing::{error, warn};

const TWENTY_FOUR_HOURS: Duration = Duration::from_secs(60 * 60 * 24);
const MAX_PENDING_CRASH_REPORTS: usize = 10;
const MIN_CONSECUTIVE_FAILED_UPDATE_CHECKS: u32 = 5;

/// Parameters to `handle_crash_reports_impl` are extracted out into a seperate type. Otherwise,
/// we'd have to pass the parameters in seperately and it would be easy to mistake the ordering
/// of the integers.
struct HandleCrashReportsParams<T: TimeSource> {
    proxy: CrashReporterProxy,
    /// A pending crash report is any crash report request that's successfully sent to the
    /// `monitor_control_requests` future, but has not yet completed (whether by completing the
    /// `file` call or deciding to skip it).
    max_pending_crash_reports: usize,
    /// The minimum number of consecutive update check failures required to file a crash report.
    min_consecutive_failed_update_checks: u32,
    time_source: T,
}

pub fn handle_crash_reports<'a, T: TimeSource + 'a>(
    proxy: CrashReporterProxy,
    time_source: T,
) -> (CrashReportControlHandle, LocalBoxFuture<'a, ()>) {
    handle_crash_reports_impl(HandleCrashReportsParams {
        proxy,
        max_pending_crash_reports: MAX_PENDING_CRASH_REPORTS,
        min_consecutive_failed_update_checks: MIN_CONSECUTIVE_FAILED_UPDATE_CHECKS,
        time_source,
    })
}

fn handle_crash_reports_impl<'a, T: TimeSource + 'a>(
    params: HandleCrashReportsParams<T>,
) -> (CrashReportControlHandle, LocalBoxFuture<'a, ()>) {
    // The capacity of the channel is max_pending_crash_reports because when it's full, we should
    // not accept any ControlRequests via `try_send`. Note: we subtract 1 because the capacity is
    // actually 1 plus the number passed in to the `channel` fn.
    let (send, recv) = mpsc::channel(params.max_pending_crash_reports - 1);
    (CrashReportControlHandle(send), monitor_control_requests(params, recv).boxed_local())
}

/// A future that receives crash report signatures over the channel, determines if the report should
/// be filed, and then uses the proxy to send a File FIDL request to the CrashReporter service.
///
/// We add this layer of abstraction so that we can:
/// 1. Deduplicate CrashReporter requests for InstallationErrors within the same 24 hour band. This
///    is important so that we don't spam the CrashReporter with redundant errors.
/// 2. Only file crash reports for ConsecutiveFailedUpdateChecks once we reach a given threshold.
/// 3. Rate-limit requests to the CrashReporter service. The CrashReporter FIDL API does not return
///    until the crash report has been fully generated, which can take many seconds. Rate-limiting
///    should help support filing crash reports in this window. Note: the maximum pending crash
///    reports are implicitly enforced by the channel capacity.
async fn monitor_control_requests<T: TimeSource>(
    params: HandleCrashReportsParams<T>,
    mut recv: mpsc::Receiver<ControlRequest>,
) {
    let mut previous_report_filed_timestamp = None;
    loop {
        match recv.next().await {
            Some(ControlRequest::InstallationError) => {
                let now = params.time_source.now_in_monotonic();
                if let Some(prev) = previous_report_filed_timestamp {
                    // Do not file InstallationError crash reports within a 24 hour period.
                    if now < prev + TWENTY_FOUR_HOURS {
                        warn!("skipping report because we already filed one in the past 24 hours");
                        continue;
                    }
                }
                file_report(&params.proxy, "fuchsia-installation-error").await;
                previous_report_filed_timestamp = Some(now);
            }
            Some(ControlRequest::ConsecutiveFailedUpdateChecks(n)) => {
                let min = params.min_consecutive_failed_update_checks;
                if (n >= min) && (n - min + 1).is_power_of_two() {
                    let signature = format!("fuchsia-{}-consecutive-failed-update-checks", n);
                    file_report(&params.proxy, &signature).await;
                }
            }
            None => {
                error!("Crash report handler failed to receive ControlRequest");
                return;
            }
        }
    }
}

/// File a crash report with the given signature. We log on error because crash-reporting is
/// best-effort and should not block update checks.
async fn file_report(proxy: &CrashReporterProxy, signature: &str) {
    if let Err(e) = proxy
        .file(CrashReport {
            crash_signature: Some(signature.to_owned()),
            program_name: Some("system".to_owned()),
            // Need to do syscall because `std::time::Instant` cannot be converted into nanos.
            program_uptime: Some(zx::Time::get_monotonic().into_nanos()),
            is_fatal: Some(false),
            ..CrashReport::EMPTY
        })
        .await
    {
        error!("Error filing crash report: {:#}", anyhow!(e));
    };
}

/// A handle to forward crash report requests to `monitor_control_requests` future.
#[derive(Debug)]
pub struct CrashReportControlHandle(mpsc::Sender<ControlRequest>);

impl CrashReportControlHandle {
    /// Forward InstallationError alerts to the CrashReporter service.
    pub fn installation_error(&mut self) -> Result<(), mpsc::TrySendError<ControlRequest>> {
        self.0.try_send(ControlRequest::InstallationError)
    }

    /// Forward ConsecutiveFailedUpdateChecks alerts to the CrashReporter service.
    pub fn consecutive_failed_update_checks(
        &mut self,
        consecutive_failed_update_checks: u32,
    ) -> Result<(), mpsc::TrySendError<ControlRequest>> {
        self.0.try_send(ControlRequest::ConsecutiveFailedUpdateChecks(
            consecutive_failed_update_checks,
        ))
    }
}

/// The set of messages than can be sent to the `monitor_control_requests` future.
#[derive(Debug)]
pub enum ControlRequest {
    InstallationError,
    ConsecutiveFailedUpdateChecks(u32),
}

#[cfg(test)]
/// Verifies the signature of the CrashReport is what's expected.
pub fn assert_signature(report: CrashReport, expected_signature: &str) {
    assert_matches::assert_matches!(
        report,
        CrashReport {
            crash_signature: Some(signature),
            program_name: Some(program),
            program_uptime: Some(_),
            is_fatal: Some(false),
            ..
        } if signature == expected_signature && program == "system"
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use fuchsia_async::{self as fasync, Task};
    use mock_crash_reporter::{MockCrashReporterService, ThrottleHook};
    use omaha_client::time::MockTimeSource;
    use std::sync::Arc;

    /// Verifies the file function behaves as expected.
    async fn test_file_crash_report(res: Result<(), zx::Status>) {
        let (hook, mut recv) = ThrottleHook::new(res);
        let mock = Arc::new(MockCrashReporterService::new(hook));
        let (proxy, _crash_report_server) = mock.spawn_crash_reporter_service();

        let () = Task::local(async move {
            let () = file_report(&proxy, "foo").await;
        })
        .detach();

        assert_signature(recv.next().await.unwrap(), "foo");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_file_crash_report_success() {
        test_file_crash_report(Ok(())).await
    }

    /// We should ignore any errors from the CrashReporter service.
    #[fasync::run_singlethreaded(test)]
    async fn test_file_crash_report_error_ignored() {
        test_file_crash_report(Err(zx::Status::NOT_FOUND)).await
    }

    /// Verifies installation error reports are deduplicated over 24 hour periods.
    #[fasync::run_singlethreaded(test)]
    async fn test_installation_error() {
        let (hook, mut recv) = ThrottleHook::new(Ok(()));
        let mock = Arc::new(MockCrashReporterService::new(hook));
        let (proxy, _fidl_server) = mock.spawn_crash_reporter_service();
        let mut time_source = MockTimeSource::new_from_now();
        let (mut ch, fut) = handle_crash_reports_impl(HandleCrashReportsParams {
            proxy,
            max_pending_crash_reports: 10,
            min_consecutive_failed_update_checks: 0,
            time_source: time_source.clone(),
        });
        let _control_request_server = fasync::Task::local(fut);

        // On the first InstallationError, we file a report.
        let () = ch.installation_error().unwrap();
        assert_signature(recv.next().await.unwrap(), "fuchsia-installation-error");

        // Subsequent requests within 24 hours should not file a report. We know we don't file a
        // report because nothing is polled from the receiver.
        let () = ch.installation_error().unwrap();
        assert_matches!(recv.try_next(), Err(_));
        time_source.advance(TWENTY_FOUR_HOURS - Duration::from_secs(1));
        let () = ch.installation_error().unwrap();
        assert_matches!(recv.try_next(), Err(_));

        // When we hit 24 hrs, we'll file a new report.
        time_source.advance(Duration::from_secs(1));
        let () = ch.installation_error().unwrap();
        assert_signature(recv.next().await.unwrap(), "fuchsia-installation-error");

        // We'll also file a new report when we exceed 24 hours.
        time_source.advance(TWENTY_FOUR_HOURS + Duration::from_secs(1));
        let () = ch.installation_error().unwrap();
        assert_signature(recv.next().await.unwrap(), "fuchsia-installation-error");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_consecutive_failed_update_checks() {
        let (hook, mut recv) = ThrottleHook::new(Ok(()));
        let mock = Arc::new(MockCrashReporterService::new(hook));
        let (proxy, _fidl_server) = mock.spawn_crash_reporter_service();
        let (mut ch, fut) = handle_crash_reports_impl(HandleCrashReportsParams {
            proxy,
            max_pending_crash_reports: 2,
            min_consecutive_failed_update_checks: 1,
            time_source: MockTimeSource::new_from_now(),
        });
        let _control_request_server = fasync::Task::local(fut);

        // If num checks < min, we SHOULD NOT file a crash report.
        let () = ch.consecutive_failed_update_checks(0).unwrap();
        assert_matches!(recv.try_next(), Err(_));

        // If num checks >= min, we SHOULD file a crash report on a backoff (e.g. 1, 2, 4, etc).
        let () = ch.consecutive_failed_update_checks(1).unwrap();
        assert_signature(recv.next().await.unwrap(), "fuchsia-1-consecutive-failed-update-checks");

        let () = ch.consecutive_failed_update_checks(2).unwrap();
        assert_signature(recv.next().await.unwrap(), "fuchsia-2-consecutive-failed-update-checks");

        let () = ch.consecutive_failed_update_checks(3).unwrap();
        assert_matches!(recv.try_next(), Err(_));

        let () = ch.consecutive_failed_update_checks(4).unwrap();
        assert_signature(recv.next().await.unwrap(), "fuchsia-4-consecutive-failed-update-checks");
    }

    /// Tests that the number of pending crash reports is correctly bounded.
    #[fasync::run_singlethreaded(test)]
    async fn test_max_pending_crash_reports() {
        let (hook, mut recv) = ThrottleHook::new(Ok(()));
        let mock = Arc::new(MockCrashReporterService::new(hook));
        let (proxy, _fidl_server) = mock.spawn_crash_reporter_service();
        let (mut ch, fut) = handle_crash_reports_impl(HandleCrashReportsParams {
            proxy,
            max_pending_crash_reports: 2,
            min_consecutive_failed_update_checks: 0,
            time_source: MockTimeSource::new_from_now(),
        });
        let _control_request_server = fasync::Task::local(fut);

        // The first control request should go through, but hang when calling `file`.
        // After this call, we're guaranteed to have 1 pending crash report.
        let () = ch.consecutive_failed_update_checks(0).unwrap();

        // After this call, we're guaranteed to have 2 pending crash reports.
        let () = ch.installation_error().unwrap();

        // Since we're at the max pending crash reports, additional requests will fail.
        assert_matches!(ch.installation_error(), Err(mpsc::TrySendError::<ControlRequest> { .. }));

        // Complete a file call, so we now have 1 pending crash report.
        assert_signature(recv.next().await.unwrap(), "fuchsia-0-consecutive-failed-update-checks");

        // Now that the file call is unblocked, we can successfully make another request.
        let () = ch.consecutive_failed_update_checks(1).unwrap();

        // Drain remaining file calls.
        assert_signature(recv.next().await.unwrap(), "fuchsia-installation-error");
        assert_signature(recv.next().await.unwrap(), "fuchsia-1-consecutive-failed-update-checks");
    }

    /// Tests that when the control handle is dropped, the `handle_crash_reports_impl` future
    /// terminates.
    #[fasync::run_singlethreaded(test)]
    async fn test_ch_dropped() {
        let mock = Arc::new(MockCrashReporterService::new(|_| Ok(())));
        let (proxy, _fidl_server) = mock.spawn_crash_reporter_service();
        let (ch, fut) = handle_crash_reports_impl(HandleCrashReportsParams {
            proxy,
            max_pending_crash_reports: 2,
            min_consecutive_failed_update_checks: 0,
            time_source: MockTimeSource::new_from_now(),
        });

        drop(ch);

        let () = fut.await;
    }
}
