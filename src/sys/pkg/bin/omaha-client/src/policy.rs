// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::BoxFuture;
use futures::prelude::*;
use log::{info, warn};
use omaha_client::{
    common::{App, CheckOptions, CheckTiming, ProtocolState, UpdateCheckSchedule},
    installer::Plan,
    policy::{CheckDecision, Policy, PolicyData, PolicyEngine, UpdateDecision},
    protocol::request::InstallSource,
    request_builder::RequestParams,
    time::{PartialComplexTime, TimeSource},
    unless::Unless,
};
use std::time::Duration;

/// We do periodic update check roughly every hour.
const PERIODIC_INTERVAL: Duration = Duration::from_secs(1 * 60 * 60);
/// Wait at least one minute before checking for updates after startup.
const STARTUP_DELAY: Duration = Duration::from_secs(60);
/// Wait 5 minutes before retrying after failed update checks.
const RETRY_DELAY: Duration = Duration::from_secs(5 * 60);

/// The policy implementation for Fuchsia.
pub struct FuchsiaPolicy;

impl Policy for FuchsiaPolicy {
    fn compute_next_update_time(
        policy_data: &PolicyData,
        _apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
    ) -> CheckTiming {
        info!(
            "FuchsiaPolicy::compute_next_update_time with {:?} and {:?} in state {:?}",
            scheduling, policy_data, protocol_state
        );

        // Error conditions are handled separately.
        //
        // For the first few consecutive errors, retry quickly using only the monotonic clock.
        // Afterwards, only use the monotonic clock, but the normal delay, until such time
        // as omaha has been reached again.
        let consecutive_failed_checks = protocol_state.consecutive_failed_update_checks;
        if consecutive_failed_checks > 0 {
            warn!("Using Retry Mode logic: {:?}", protocol_state);
            let error_duration =
                if consecutive_failed_checks < 4 { RETRY_DELAY } else { PERIODIC_INTERVAL };
            return CheckTiming::builder()
                .time(PartialComplexTime::Monotonic(
                    (policy_data.current_time + error_duration).into(),
                ))
                .build();
        }

        // Normal operation, use the standard poll interval, unless a server-dictated interval
        // has been set by the server.
        let interval = PERIODIC_INTERVAL.unless(protocol_state.server_dictated_poll_interval);

        // The `CheckTiming` to return is primarily based on the state of the `last_update_time`.
        //
        // If this is the first attempt to talk to Omaha since starting, `last_update_time` will
        // be `None`, or will not have an Instant component, and so there needs to be a minimum wait
        // of STARTUP_DELAY, so that no automatic background polls are performed too soon after
        // startup.
        match scheduling.last_update_time {
            // There is no previous time at all, so this looks like the very first time (or after
            // say a factory reset), so go immediately after the startup delay from now.
            None => {
                warn!("Using FDR Startup Mode logic.");
                CheckTiming::builder()
                    .time(policy_data.current_time + STARTUP_DELAY)
                    .minimum_wait(STARTUP_DELAY)
                    .build()
            }

            // If there's a `last_update_time`, then the time for the next check is at least
            // partially based on that.
            Some(last_update_time) => {
                match last_update_time {
                    // If only a wall time is known, then it's likely that this is a startup
                    // condition, and bound on the monotonic timeline needs to be added to the
                    // bound based on the last update wall time, as well as a minimum delay.
                    //
                    // PartialComplexTime::complete_with(ComplexTime) accomplises this by adding
                    // the missing bound on the monotonic timeline.
                    last_wall_time @ PartialComplexTime::Wall(_) => {
                        info!("Using Startup Mode logic.");
                        CheckTiming::builder()
                            .time(last_wall_time.complete_with(policy_data.current_time) + interval)
                            .minimum_wait(STARTUP_DELAY)
                            .build()
                    }

                    // In all other cases (there is at least a monotonic time), add the interval to
                    // the last time and use that.
                    last_update_time @ _ => {
                        info!("Using Standard logic.");
                        CheckTiming::builder().time(last_update_time + interval).build()
                    }
                }
            }
        }
    }

    fn update_check_allowed(
        policy_data: &PolicyData,
        _apps: &[App],
        scheduling: &UpdateCheckSchedule,
        _protocol_state: &ProtocolState,
        check_options: &CheckOptions,
    ) -> CheckDecision {
        info!(
            "FuchsiaPolicy::update_check_allowed with {:?} and {:?} for {:?}",
            scheduling, policy_data, check_options
        );
        // Always allow update check initiated by a user.
        if check_options.source == InstallSource::OnDemand {
            CheckDecision::Ok(RequestParams {
                source: InstallSource::OnDemand,
                use_configured_proxies: true,
            })
        } else {
            match scheduling.next_update_time {
                // Cannot check without first scheduling a next update time.
                None => CheckDecision::TooSoon,
                Some(next_update_time) => {
                    if policy_data.current_time.is_after_or_eq_any(next_update_time.time) {
                        CheckDecision::Ok(RequestParams {
                            source: InstallSource::ScheduledTask,
                            use_configured_proxies: true,
                        })
                    } else {
                        CheckDecision::TooSoon
                    }
                }
            }
        }
    }

    fn update_can_start(
        _policy_data: &PolicyData,
        _proposed_install_plan: &impl Plan,
    ) -> UpdateDecision {
        UpdateDecision::Ok
    }
}

/// FuchsiaPolicyEngine just gathers the current time and hands it off to the FuchsiaPolicy as the
/// PolicyData.
pub struct FuchsiaPolicyEngine<T: TimeSource> {
    time_source: T,
}
pub struct FuchsiaPolicyEngineBuilder;
pub struct FuchsiaPolicyEngineBuilderWithTime<T: TimeSource> {
    time_source: T,
}
impl FuchsiaPolicyEngineBuilder {
    pub fn time_source<T: TimeSource>(
        self,
        time_source: T,
    ) -> FuchsiaPolicyEngineBuilderWithTime<T> {
        FuchsiaPolicyEngineBuilderWithTime { time_source }
    }
}
impl<T: TimeSource> FuchsiaPolicyEngineBuilderWithTime<T> {
    pub fn build(self) -> FuchsiaPolicyEngine<T> {
        FuchsiaPolicyEngine { time_source: self.time_source }
    }
}

impl<T: TimeSource> PolicyEngine for FuchsiaPolicyEngine<T> {
    fn compute_next_update_time(
        &mut self,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
    ) -> BoxFuture<'_, CheckTiming> {
        let timing = FuchsiaPolicy::compute_next_update_time(
            &PolicyData::builder().use_timesource(&self.time_source).build(),
            apps,
            scheduling,
            protocol_state,
        );
        future::ready(timing).boxed()
    }

    fn update_check_allowed(
        &mut self,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
        check_options: &CheckOptions,
    ) -> BoxFuture<'_, CheckDecision> {
        let decision = FuchsiaPolicy::update_check_allowed(
            &PolicyData::builder().use_timesource(&self.time_source).build(),
            apps,
            scheduling,
            protocol_state,
            check_options,
        );
        future::ready(decision).boxed()
    }

    fn update_can_start(
        &mut self,
        proposed_install_plan: &impl Plan,
    ) -> BoxFuture<'_, UpdateDecision> {
        let decision = FuchsiaPolicy::update_can_start(
            &PolicyData::builder().use_timesource(&self.time_source).build(),
            proposed_install_plan,
        );
        future::ready(decision).boxed()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use omaha_client::installer::stub::StubPlan;
    use omaha_client::time::{ComplexTime, MockTimeSource};
    use std::time::Instant;

    /// Test that the correct next update time is calculated for the normal case where a check was
    /// recently done and the next needs to be scheduled.
    #[test]
    fn test_compute_next_update_time_for_normal_operation() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was recently in the past
        let last_update_time = now - Duration::from_secs(1234);
        let schedule = UpdateCheckSchedule::builder().last_time(last_update_time).build();
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = PolicyData::builder().time(now).build();
        // Execute the policy check.
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        // Confirm that:
        //  - the policy-computed next update time is a standard poll interval from the last.
        let expected = CheckTiming::builder()
            .time(schedule.last_update_time.unwrap() + PERIODIC_INTERVAL)
            .build();
        debug_print_check_timing_test_data(
            "normal operation",
            expected,
            result,
            last_update_time,
            now,
        );
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated at first startup, when there is no
    /// stored `last_update_time`.
    ///
    /// This is the case of a bootup right after a factory reset.
    #[test]
    fn test_compute_next_update_time_at_first_startup() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - there is no last update time
        let schedule = UpdateCheckSchedule::builder().build();
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = PolicyData::builder().time(now).build();
        // Execute the policy check.
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        // Confirm that:
        //  - the policy-computed next update time is a startup delay poll interval from now.
        let expected =
            CheckTiming::builder().time(now + STARTUP_DELAY).minimum_wait(STARTUP_DELAY).build();
        debug_print_check_timing_test_data("first startup", expected, result, None, now);
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated at startup when the persisted last
    /// update time is "far" into the past.  This is an unlikely corner case.  But it could happen
    /// if there was a persisted `last_update_time` that was from an update check that succeeded
    /// while on the previous build's backstop time.
    #[test]
    fn test_compute_next_update_time_at_startup_with_past_last_update_time() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was far in the past (a bit over a day)
        //   - persisted times will not have monotonic components, or sub-millisecond precision.
        let last_update_time = now - Duration::from_secs(100000);
        let last_update_time_persisted = last_update_time.checked_to_micros_since_epoch().unwrap();
        let last_update_time =
            PartialComplexTime::from_micros_since_epoch(last_update_time_persisted);
        let schedule = UpdateCheckSchedule::builder().last_time(last_update_time).build();
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = PolicyData::builder().time(now).build();
        // Execute the policy check.
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        // Confirm that:
        //  - The policy-computed next update time is
        //       1) a normal poll interval from now (monotonic)
        //       2) a normal poll interval from the persisted last update time (wall)
        //  - There's a minimum wait of STARTUP_DELAY
        let expected = CheckTiming::builder()
            .time((
                last_update_time.checked_to_system_time().unwrap() + PERIODIC_INTERVAL,
                Instant::from(now) + PERIODIC_INTERVAL,
            ))
            .minimum_wait(STARTUP_DELAY)
            .build();
        debug_print_check_timing_test_data(
            "startup with past last_update_time",
            expected,
            result,
            last_update_time,
            now,
        );
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated at startup when the persisted last
    /// update time is in the future.
    ///
    /// This specifically catches the case when starting on the backstop time, and the persisted
    /// time is "ahead" of now, as the current time hasn't been sync'd with UTC.  This is the normal
    /// startup case for most boots of a device.
    #[test]
    fn test_compute_next_update_time_at_startup_with_future_last_update_time() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was far in the future (a bit over 12 days)
        //   - persisted times will not have monotonic components, or sub-millisecond precision.
        let last_update_time = now + Duration::from_secs(1000000);
        let last_update_time_persisted = last_update_time.checked_to_micros_since_epoch().unwrap();
        let last_update_time =
            PartialComplexTime::from_micros_since_epoch(last_update_time_persisted);
        let schedule = UpdateCheckSchedule::builder().last_time(last_update_time).build();
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = PolicyData::builder().time(now).build();
        // Execute the policy check.
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        // Confirm that:
        //  - The policy-computed next update time is
        //       1) a normal interval from now (monotonic)
        //       2) a normal interval from the persisted last update time (wall)
        //  - There's a minimum wait of STARTUP_DELAY
        let expected = CheckTiming::builder()
            .time((
                last_update_time.checked_to_system_time().unwrap() + PERIODIC_INTERVAL,
                Instant::from(now) + PERIODIC_INTERVAL,
            ))
            .minimum_wait(STARTUP_DELAY)
            .build();
        debug_print_check_timing_test_data(
            "startup with future last_update_time",
            expected,
            result,
            last_update_time,
            now,
        );
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated for the case when there is a single
    /// update check failure.
    #[test]
    fn test_compute_next_update_time_after_a_single_failure() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was in the past
        //   - there is 1 failed update check, which moves the policy to "fast" retries
        let last_update_time = now - Duration::from_secs(10);
        let schedule = UpdateCheckSchedule::builder().last_time(last_update_time).build();
        let protocol_state =
            ProtocolState { consecutive_failed_update_checks: 1, ..Default::default() };
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = PolicyData::builder().time(now).build();
        // Execute the policy check
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        // Confirm that:
        //  - the computed next update time is a retry poll interval from now, on the monotonic
        //    timeline only.
        let expected = CheckTiming::builder().time(now.mono + RETRY_DELAY).build();
        debug_print_check_timing_test_data(
            "single failure",
            expected,
            result,
            last_update_time,
            now,
        );
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated for the case when there are multiple,
    /// consecutive update check failures.
    ///
    /// TODO:  The current test setup is for a first-attempt-after-startup, but that doesn't align
    ///        with the consecutive test failures (there would need to be a next_update_time)
    #[test]
    fn test_compute_next_update_time_after_many_consecutive_failures() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was in the past
        //   - there are 4 failed update checks, which moves the policy from fast retries to slow
        let last_update_time = now - Duration::from_secs(10);
        let schedule = UpdateCheckSchedule::builder().last_time(last_update_time).build();
        let protocol_state =
            ProtocolState { consecutive_failed_update_checks: 4, ..Default::default() };
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = PolicyData::builder().time(now).build();
        // Execute the policy check
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        // Confirm that:
        //  - the computed next update time is a standard poll interval from now (only monotonic).
        let expected = CheckTiming::builder().time(now.mono + PERIODIC_INTERVAL).build();
        debug_print_check_timing_test_data(
            "many failures",
            expected,
            result,
            last_update_time,
            now,
        );
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated when there is a server-dictated poll
    /// interval in effect.
    #[test]
    fn test_compute_next_update_time_uses_server_dictated_poll_interval_if_present() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was recently in the past
        //   - there is no planned next update time
        // TODO: This should be changed to there being a next_update_time (which is in the past, and
        //       likely to be "before" the last_update_check by the duration it takes to perform an
        //       update check)
        let schedule =
            UpdateCheckSchedule::builder().last_time(now - Duration::from_secs(1234)).build();
        let server_dictated_poll_interval = Duration::from_secs(5678);
        let protocol_state = ProtocolState {
            server_dictated_poll_interval: Some(server_dictated_poll_interval),
            ..Default::default()
        };
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = PolicyData::builder().time(now).build();
        // Execute the policy check
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        // Confirm that:
        //  - the last update check time is unchanged.
        //  - the computed next update time is a server-dictated poll interval from now.
        let expected = CheckTiming::builder()
            .time(schedule.last_update_time.unwrap() + server_dictated_poll_interval)
            .build();
        debug_print_check_timing_test_data("server dictated", expected, result, None, now);
        assert_eq!(result, expected);
    }

    // Test that an update check is allowed after the next_update_time has passed.
    #[test]
    fn test_update_check_allowed_after_next_update_time_is_ok() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was far in the past
        //   - the next update time is just in the past
        let last_update_time = now - PERIODIC_INTERVAL - Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL;
        let schedule = UpdateCheckSchedule::builder()
            .last_time(last_update_time)
            .next_timing(CheckTiming::builder().time(next_update_time).build())
            .build();
        // Set up the state for this check:
        //  - the time is "now"
        //  - the check options are at normal defaults (scheduled background check)
        let policy_data = PolicyData::builder().time(now).build();
        let check_options = CheckOptions::default();
        // Execute the policy check
        let result = FuchsiaPolicy::update_check_allowed(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
            &check_options,
        );
        // Confirm that:
        //  - the decision is Ok
        //  - the source is the same as the check_options' source
        let expected = CheckDecision::Ok(RequestParams {
            source: check_options.source,
            use_configured_proxies: true,
        });
        assert_eq!(result, expected);
    }

    // Test that an update check is not allowed before the next_update_time.
    #[test]
    fn test_update_check_allowed_before_next_update_time_is_too_soon() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was far in the past
        //   - the next update time is in the future
        let last_update_time = now - PERIODIC_INTERVAL + Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL;
        let schedule = UpdateCheckSchedule::builder()
            .last_time(last_update_time)
            .next_timing(CheckTiming::builder().time(next_update_time).build())
            .build();
        // Set up the state for this check:
        //  - the time is "now"
        //  - the check options are at normal defaults (scheduled background check)
        let policy_data = PolicyData::builder().time(now).build();
        // Execute the policy check
        let result = FuchsiaPolicy::update_check_allowed(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
            &CheckOptions::default(),
        );
        // Confirm that:
        //  - the decision is "TooSoon"
        assert_eq!(result, CheckDecision::TooSoon);
    }

    // Test that update_can_start returns Ok (always)
    #[test]
    fn test_update_can_start_always_ok() {
        let policy_data =
            PolicyData::builder().use_timesource(&MockTimeSource::new_from_now()).build();
        let result = FuchsiaPolicy::update_can_start(&policy_data, &StubPlan);
        assert_eq!(result, UpdateDecision::Ok);
    }

    /// Prints a bunch of context about a test for proper CheckTiming generation, to stderr, for
    /// capturing as part of a failed test.
    fn debug_print_check_timing_test_data<T>(
        log_tag: &str,
        expected: CheckTiming,
        result: CheckTiming,
        last_update_time: T,
        now: ComplexTime,
    ) where
        T: Into<Option<PartialComplexTime>>,
    {
        if result != expected {
            let last_update_time = last_update_time.into();
            eprintln!(
                "[{}] last_update_time: {}",
                log_tag,
                omaha_client::common::PrettyOptionDisplay(last_update_time)
            );
            eprintln!("[{}]              now: {}", log_tag, now);
            eprintln!("[{}] expected: {}", log_tag, expected);
            eprintln!("[{}]   result: {}", log_tag, result);
            if let Some(last_update_time) = last_update_time {
                if let Some(last_update_time) = last_update_time.checked_to_system_time() {
                    eprintln!(
                        "[{}] expected wall duration (from last_update_time): {:?}",
                        log_tag,
                        expected
                            .time
                            .checked_to_system_time()
                            .map(|t| t.duration_since(last_update_time))
                    );
                    eprintln!(
                        "[{}]   result wall duration (from last_update_time): {:?}",
                        log_tag,
                        result
                            .time
                            .checked_to_system_time()
                            .map(|t| t.duration_since(last_update_time))
                    );
                }
                eprintln!(
                    "[{}]                    result wall duration (from now): {:?}",
                    log_tag,
                    result.time.checked_to_system_time().map(|t| t.duration_since(now.into()))
                );
                if let Some(last_update_time) = last_update_time.checked_to_instant() {
                    eprintln!(
                        "[{}] expected mono duration (from last_update_time): {:?}",
                        log_tag,
                        expected
                            .time
                            .checked_to_instant()
                            .map(|t| t.duration_since(last_update_time))
                    );
                    eprintln!(
                        "[{}]   result mono duration (from last_update_time): {:?}",
                        log_tag,
                        result
                            .time
                            .checked_to_instant()
                            .map(|t| t.duration_since(last_update_time))
                    );
                }
            }
            eprintln!(
                "[{}]                    result mono duration (from now): {:?}",
                log_tag,
                result.time.checked_to_instant().map(|t| t.duration_since(now.into()))
            );
        }
    }
}
