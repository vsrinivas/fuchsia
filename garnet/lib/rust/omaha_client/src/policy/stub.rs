// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    clock,
    common::{App, CheckOptions, ProtocolState, UpdateCheckSchedule},
    installer::Plan,
    policy::{CheckDecision, Policy, PolicyData, PolicyEngine, UpdateDecision},
    request_builder::RequestParams,
};
use futures::future::BoxFuture;
use futures::prelude::*;

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
        _proposed_install_plan: &impl Plan,
    ) -> UpdateDecision {
        UpdateDecision::Ok
    }
}

/// A stub PolicyEngine that just gathers the current time and hands it off to the StubPolicy as the
/// PolicyData.
#[derive(Debug)]
pub struct StubPolicyEngine;

impl PolicyEngine for StubPolicyEngine {
    fn compute_next_update_time(
        &mut self,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
    ) -> BoxFuture<'_, UpdateCheckSchedule> {
        let schedule = StubPolicy::compute_next_update_time(
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
        let decision = StubPolicy::update_check_allowed(
            &PolicyData { current_time: clock::now() },
            apps,
            scheduling,
            protocol_state,
            check_options,
        );
        future::ready(decision).boxed()
    }

    fn update_can_start(&mut self, proposed_install_plan: &impl Plan) -> BoxFuture<'_, UpdateDecision> {
        let decision = StubPolicy::update_can_start(
            &PolicyData { current_time: clock::now() },
            proposed_install_plan,
        );
        future::ready(decision).boxed()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{installer::stub::StubPlan, protocol::request::InstallSource};
    use std::time::SystemTime;

    const SCHEDULING: UpdateCheckSchedule = UpdateCheckSchedule {
        last_update_time: SystemTime::UNIX_EPOCH,
        next_update_window_start: SystemTime::UNIX_EPOCH,
        next_update_time: SystemTime::UNIX_EPOCH,
    };

    #[test]
    fn test_compute_next_update_time() {
        let now = clock::now();
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
        let policy_data = PolicyData { current_time: clock::now() };
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
        let policy_data = PolicyData { current_time: clock::now() };
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
        let policy_data = PolicyData { current_time: clock::now() };
        let result = StubPolicy::update_can_start(&policy_data, &StubPlan);
        assert_eq!(result, UpdateDecision::Ok);
    }
}
