// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::BoxFuture;
use futures::prelude::*;
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

/// We do periodic update check roughly every 5 hours.
const PERIODIC_INTERVAL: Duration = Duration::from_secs(5 * 60 * 60);
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
        UpdateCheckSchedule {
            last_update_time: scheduling.last_update_time,
            next_update_window_start: next_update_time,
            next_update_time,
        }
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

    fn update_can_start(&mut self, proposed_install_plan: &impl Plan) -> BoxFuture<'_, UpdateDecision> {
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
    use std::time::SystemTime;

    #[test]
    fn test_compute_next_update_time() {
        let now = clock::now();
        let policy_data = PolicyData { current_time: now };
        let last_update_time = now - Duration::from_secs(1234);
        let schedule = UpdateCheckSchedule {
            last_update_time,
            next_update_window_start: SystemTime::UNIX_EPOCH,
            next_update_time: SystemTime::UNIX_EPOCH,
        };
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        let next_update_time = last_update_time + PERIODIC_INTERVAL;
        let expected = UpdateCheckSchedule {
            last_update_time,
            next_update_window_start: next_update_time,
            next_update_time,
        };
        assert_eq!(result, expected);
    }

    #[test]
    fn test_compute_next_update_time_startup() {
        let now = clock::now();
        let policy_data = PolicyData { current_time: now };
        let last_update_time = now - Duration::from_secs(123456);
        let schedule = UpdateCheckSchedule {
            last_update_time,
            next_update_window_start: SystemTime::UNIX_EPOCH,
            next_update_time: SystemTime::UNIX_EPOCH,
        };
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        let next_update_time = now + STARTUP_DELAY;
        let expected = UpdateCheckSchedule {
            last_update_time,
            next_update_window_start: next_update_time,
            next_update_time,
        };
        assert_eq!(result, expected);
    }

    #[test]
    fn test_compute_next_update_time_single_failure() {
        let now = clock::now();
        let policy_data = PolicyData { current_time: now };
        let last_update_time = now - Duration::from_secs(123456);
        let schedule = UpdateCheckSchedule {
            last_update_time,
            next_update_window_start: SystemTime::UNIX_EPOCH,
            next_update_time: SystemTime::UNIX_EPOCH,
        };
        let mut protocol_state = ProtocolState::default();
        protocol_state.consecutive_failed_update_checks = 1;
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        let next_update_time = now + RETRY_DELAY;
        let expected = UpdateCheckSchedule {
            last_update_time,
            next_update_window_start: next_update_time,
            next_update_time,
        };
        assert_eq!(result, expected);
    }

    #[test]
    fn test_compute_next_update_time_consecutive_failures() {
        let now = clock::now();
        let policy_data = PolicyData { current_time: now };
        let last_update_time = now - Duration::from_secs(123456);
        let schedule = UpdateCheckSchedule {
            last_update_time,
            next_update_window_start: SystemTime::UNIX_EPOCH,
            next_update_time: SystemTime::UNIX_EPOCH,
        };
        let mut protocol_state = ProtocolState::default();
        protocol_state.consecutive_failed_update_checks = 4;
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        let next_update_time = now + PERIODIC_INTERVAL;
        let expected = UpdateCheckSchedule {
            last_update_time,
            next_update_window_start: next_update_time,
            next_update_time,
        };
        assert_eq!(result, expected);
    }

    #[test]
    fn test_server_dictated_poll_interval() {
        let now = clock::now();
        let policy_data = PolicyData { current_time: now };
        let last_update_time = now - Duration::from_secs(1234);
        let interval = Duration::from_secs(5678);
        let next_update_time = last_update_time + interval;
        let schedule = UpdateCheckSchedule {
            last_update_time,
            next_update_window_start: SystemTime::UNIX_EPOCH,
            next_update_time: SystemTime::UNIX_EPOCH,
        };
        let protocol_state = ProtocolState {
            server_dictated_poll_interval: Some(interval),
            ..ProtocolState::default()
        };
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        let expected = UpdateCheckSchedule {
            last_update_time,
            next_update_window_start: next_update_time,
            next_update_time,
        };
        assert_eq!(result, expected);
    }

    #[test]
    fn test_update_check_allowed_ok() {
        let now = clock::now();
        let policy_data = PolicyData { current_time: now };
        let last_update_time = now - PERIODIC_INTERVAL - Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL;
        let schedule = UpdateCheckSchedule {
            last_update_time: last_update_time,
            next_update_window_start: next_update_time,
            next_update_time,
        };
        let check_options = CheckOptions::default();
        let result = FuchsiaPolicy::update_check_allowed(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
            &check_options,
        );
        let expected = CheckDecision::Ok(RequestParams {
            source: check_options.source,
            use_configured_proxies: true,
        });
        assert_eq!(result, expected);
    }

    #[test]
    fn test_update_check_allowed_too_soon() {
        let now = clock::now();
        let policy_data = PolicyData { current_time: now };
        let last_update_time = now - PERIODIC_INTERVAL + Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL;
        let schedule = UpdateCheckSchedule {
            last_update_time: last_update_time,
            next_update_window_start: next_update_time,
            next_update_time,
        };
        let result = FuchsiaPolicy::update_check_allowed(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
            &CheckOptions::default(),
        );
        assert_eq!(result, CheckDecision::TooSoon);
    }

    #[test]
    fn test_update_can_start() {
        let policy_data = PolicyData { current_time: clock::now() };
        let result = FuchsiaPolicy::update_can_start(&policy_data, &StubPlan);
        assert_eq!(result, UpdateDecision::Ok);
    }
}
