// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::BoxFuture;
use futures::prelude::*;
use log::info;
use omaha_client::{
    clock,
    common::{App, CheckOptions, ProtocolState, UpdateCheckSchedule},
    installer::Plan,
    policy::{CheckDecision, Policy, PolicyData, PolicyEngine, UpdateDecision},
    protocol::request::InstallSource,
    request_builder::RequestParams,
};
use std::cmp::max;
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
    ) -> UpdateCheckSchedule {
        info!(
            "FuchsiaPolicy::UpdateCheckSchedule last_update_time={:?}, current_time={:?}",
            scheduling.last_update_time, policy_data.current_time
        );
        // Use server dictated interval if exists, otherwise default to 5 hours.
        let interval = protocol_state.server_dictated_poll_interval.unwrap_or(PERIODIC_INTERVAL);
        let mut next_update_time = scheduling.last_update_time + interval;
        // If we didn't talk to Omaha in the last update check, the `last_update_time` won't be
        // updated, and we need to have to a minimum delay time based on number of consecutive
        // failed update checks.
        let min_delay = if protocol_state.consecutive_failed_update_checks > 3 {
            interval
        } else if protocol_state.consecutive_failed_update_checks > 0 {
            RETRY_DELAY
        } else {
            STARTUP_DELAY
        };
        next_update_time = max(next_update_time, policy_data.current_time + min_delay);
        UpdateCheckSchedule::builder()
            .last_time(scheduling.last_update_time)
            .next_time(next_update_time)
            .build()
    }

    fn update_check_allowed(
        policy_data: &PolicyData,
        _apps: &[App],
        scheduling: &UpdateCheckSchedule,
        _protocol_state: &ProtocolState,
        check_options: &CheckOptions,
    ) -> CheckDecision {
        // Always allow update check initiated by a user.
        if check_options.source == InstallSource::OnDemand {
            CheckDecision::Ok(RequestParams {
                source: InstallSource::OnDemand,
                use_configured_proxies: true,
            })
        } else if policy_data.current_time >= scheduling.next_update_time {
            CheckDecision::Ok(RequestParams {
                source: InstallSource::ScheduledTask,
                use_configured_proxies: true,
            })
        } else {
            CheckDecision::TooSoon
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
pub struct FuchsiaPolicyEngine;

impl PolicyEngine for FuchsiaPolicyEngine {
    fn compute_next_update_time(
        &mut self,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
    ) -> BoxFuture<'_, UpdateCheckSchedule> {
        let schedule = FuchsiaPolicy::compute_next_update_time(
            &PolicyData { current_time: clock::now() },
            apps,
            scheduling,
            protocol_state,
        );
        future::ready(schedule).boxed()
    }

    fn update_check_allowed(
        &mut self,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
        check_options: &CheckOptions,
    ) -> BoxFuture<'_, CheckDecision> {
        let decision = FuchsiaPolicy::update_check_allowed(
            &PolicyData { current_time: clock::now() },
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
            &PolicyData { current_time: clock::now() },
            proposed_install_plan,
        );
        future::ready(decision).boxed()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use omaha_client::installer::stub::StubPlan;
    use pretty_assertions::assert_eq;

    /// Test that the correct next update time is calculated for the normal case where a check was
    /// recently done and the next needs to be scheduled.
    #[test]
    fn test_compute_next_update_time_for_normal_operation() {
        let now = clock::now();
        // The current context:
        //   - the last update was recently in the past
        //   - there is no planned next update time
        // TODO: This should be changed to there being a next_update_time (which is in the past, and
        //       likely to be "before" the last_update_check by the duration it takes to perform an
        //       update check)
        let schedule =
            UpdateCheckSchedule::builder().last_time(now - Duration::from_secs(1234)).build();
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = PolicyData { current_time: now };
        // Execute the policy check.
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        // Confirm that:
        //  - the last update check time is unchanged.
        //  - the policy-computed next update time is a standard poll interval from then.
        let expected = UpdateCheckSchedule::builder()
            .last_time(schedule.last_update_time)
            .next_time(schedule.last_update_time + PERIODIC_INTERVAL)
            .build();
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated at startup.
    /// This test is different from the above due to the length of the time into the past that the
    /// last update was performed.
    ///
    /// TODO:  This needs to be based off of a better trigger than the time into the past (e.g. the
    ///        lack of a previously computed next_update_time).
    #[test]
    fn test_compute_next_update_time_at_startup() {
        let now = clock::now();
        // The current context:
        //   - the last update was far in the past
        //   - there is no planned next update time
        let schedule =
            UpdateCheckSchedule::builder().last_time(now - Duration::from_secs(123456)).build();
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = PolicyData { current_time: now };
        // Execute the policy check.
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        // Confirm that:
        //  - the last update check time is unchanged.
        //  - the policy-computed next update time is a startup delay poll interval from now.
        let expected = UpdateCheckSchedule::builder()
            .last_time(schedule.last_update_time)
            .next_time(now + STARTUP_DELAY)
            .build();
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated for the case when there is a single
    /// update check failure.
    #[test]
    fn test_compute_next_update_time_after_a_single_failure() {
        let now = clock::now();
        // The current context:
        //   - the last update was in the past
        //   - there is no planned next update time
        //   - there is 1 failed update check, which moves the policy to "fast" retries
        let schedule =
            UpdateCheckSchedule::builder().last_time(now - Duration::from_secs(123456)).build();
        let protocol_state =
            ProtocolState { consecutive_failed_update_checks: 1, ..Default::default() };
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = PolicyData { current_time: now };
        // Execute the policy check
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        // Confirm that:
        //  - the last update check time is unchanged.
        //  - the computed next update time is a retry poll interval from now.
        let expected = UpdateCheckSchedule::builder()
            .last_time(schedule.last_update_time)
            .next_time(now + RETRY_DELAY)
            .build();
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated for the case when there are multiple,
    /// consecutive update check failures.
    ///
    /// TODO:  The current test setup is for a first-attempt-after-startup, but that doesn't align
    ///        with the consecutive test failures (there would need to be a next_update_time)
    #[test]
    fn test_compute_next_update_time_after_many_consecutive_failures() {
        let now = clock::now();
        // The current context:
        //   - the last update was in the past
        //   - there is no planned next update time
        //   - there are 4 failed update checks, which moves the policy from fast retries to slow
        let schedule =
            UpdateCheckSchedule::builder().last_time(now - Duration::from_secs(123456)).build();
        let protocol_state =
            ProtocolState { consecutive_failed_update_checks: 4, ..Default::default() };
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = PolicyData { current_time: now };
        // Execute the policy check
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        // Confirm that:
        //  - the last update check time is unchanged.
        //  - the computed next update time is a standard poll interval from now.
        let expected = UpdateCheckSchedule::builder()
            .last_time(schedule.last_update_time)
            .next_time(now + PERIODIC_INTERVAL)
            .build();
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated when there is a server-dictated poll
    /// interval in effect.
    #[test]
    fn test_compute_next_update_time_uses_server_dictated_poll_interval_if_present() {
        let now = clock::now();
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
        let policy_data = PolicyData { current_time: now };
        // Execute the policy check
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        // Confirm that:
        //  - the last update check time is unchanged.
        //  - the computed next update time is a server-dictated poll interval from now.
        let expected = UpdateCheckSchedule::builder()
            .last_time(schedule.last_update_time)
            .next_time(schedule.last_update_time + server_dictated_poll_interval)
            .build();
        assert_eq!(result, expected);
    }

    // Test that an update check is allowed after the next_update_time has passed.
    #[test]
    fn test_update_check_allowed_after_next_update_time_is_ok() {
        let now = clock::now();
        // The current context:
        //   - the last update was far in the past
        //   - the next update time is just in the past
        let last_update_time = now - PERIODIC_INTERVAL - Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL;
        let schedule = UpdateCheckSchedule::builder()
            .last_time(last_update_time)
            .next_time(next_update_time)
            .build();
        // Set up the state for this check:
        //  - the time is "now"
        //  - the check options are at normal defaults (scheduled background check)
        let policy_data = PolicyData { current_time: now };
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
        let now = clock::now();
        // The current context:
        //   - the last update was far in the past
        //   - the next update time is in the future
        let last_update_time = now - PERIODIC_INTERVAL + Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL;
        let schedule = UpdateCheckSchedule::builder()
            .last_time(last_update_time)
            .next_time(next_update_time)
            .build();
        // Set up the state for this check:
        //  - the time is "now"
        //  - the check options are at normal defaults (scheduled background check)
        let policy_data = PolicyData { current_time: now };
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
        let policy_data = PolicyData { current_time: clock::now() };
        let result = FuchsiaPolicy::update_can_start(&policy_data, &StubPlan);
        assert_eq!(result, UpdateDecision::Ok);
    }
}
