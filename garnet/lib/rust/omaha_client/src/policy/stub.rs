// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::super::{
    common::{App, CheckOptions, ProtocolState, UpdateCheckSchedule},
    install_plan::InstallPlan,
    requests::RequestParams,
};
use super::{CheckDecision, Policy, PolicyData, UpdateDecision};

/// A stub policy implementation that allows everything immediately.
pub struct StubPolicy;

impl Policy for StubPolicy {
    fn compute_next_update_time(
        policy_data: &PolicyData,
        _apps: &[App],
        scheduling: &UpdateCheckSchedule,
        _protocol_state: &ProtocolState,
    ) -> UpdateCheckSchedule {
        UpdateCheckSchedule {
            last_update_time: scheduling.last_update_time,
            next_update_window_start: policy_data.current_time,
            next_update_time: policy_data.current_time,
        }
    }

    fn update_check_allowed(
        _policy_data: &PolicyData,
        _apps: &[App],
        _scheduling: &UpdateCheckSchedule,
        _protocol_state: &ProtocolState,
        check_options: &CheckOptions,
    ) -> CheckDecision {
        CheckDecision::Ok(RequestParams {
            source: check_options.source.clone(),
            use_configured_proxies: true,
        })
    }

    fn update_can_start(
        _policy_data: &PolicyData,
        _proposed_install_plan: &impl InstallPlan,
    ) -> UpdateDecision {
        UpdateDecision::Ok
    }
}

#[cfg(test)]
mod tests {
    use super::super::super::{install_plan::StubInstallPlan, protocol::request::InstallSource};
    use super::*;
    use std::time::SystemTime;

    const SCHEDULING: UpdateCheckSchedule = UpdateCheckSchedule {
        last_update_time: SystemTime::UNIX_EPOCH,
        next_update_window_start: SystemTime::UNIX_EPOCH,
        next_update_time: SystemTime::UNIX_EPOCH,
    };

    #[test]
    fn test_compute_next_update_time() {
        let now = SystemTime::now();
        let policy_data = PolicyData { current_time: now };
        let result = StubPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &SCHEDULING,
            &ProtocolState::default(),
        );
        let expected = UpdateCheckSchedule {
            last_update_time: SCHEDULING.last_update_time,
            next_update_window_start: now,
            next_update_time: now,
        };
        assert_eq!(result, expected);
    }

    #[test]
    fn test_update_check_allowed_on_demand() {
        let policy_data = PolicyData { current_time: SystemTime::now() };
        let check_options = CheckOptions { source: InstallSource::OnDemand };
        let result = StubPolicy::update_check_allowed(
            &policy_data,
            &[],
            &SCHEDULING,
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
    fn test_update_check_allowed_scheduled_task() {
        let policy_data = PolicyData { current_time: SystemTime::now() };
        let check_options = CheckOptions { source: InstallSource::ScheduledTask };
        let result = StubPolicy::update_check_allowed(
            &policy_data,
            &[],
            &SCHEDULING,
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
    fn test_update_can_start() {
        let policy_data = PolicyData { current_time: SystemTime::now() };
        let result = StubPolicy::update_can_start(&policy_data, &StubInstallPlan);
        assert_eq!(result, UpdateDecision::Ok);
    }
}
