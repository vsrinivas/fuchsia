// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{App, CheckOptions, CheckTiming, ProtocolState, UpdateCheckSchedule},
    installer::Plan,
    policy::{CheckDecision, Policy, PolicyData, PolicyEngine, UpdateDecision},
    request_builder::RequestParams,
    time::TimeSource,
};
use futures::future::BoxFuture;
use futures::prelude::*;

/// A stub policy implementation that allows everything immediately.
pub struct StubPolicy;

impl Policy for StubPolicy {
    type UpdatePolicyData = PolicyData;
    type RebootPolicyData = ();
    type UpdateCanStartPolicyData = ();

    fn compute_next_update_time(
        policy_data: &Self::UpdatePolicyData,
        _apps: &[App],
        _scheduling: &UpdateCheckSchedule,
        _protocol_state: &ProtocolState,
    ) -> CheckTiming {
        CheckTiming::builder().time(policy_data.current_time).build()
    }

    fn update_check_allowed(
        _policy_data: &Self::UpdatePolicyData,
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
        _policy_data: &Self::UpdateCanStartPolicyData,
        _proposed_install_plan: &impl Plan,
    ) -> UpdateDecision {
        UpdateDecision::Ok
    }

    fn reboot_allowed(
        _policy_data: &Self::RebootPolicyData,
        _check_options: &CheckOptions,
    ) -> bool {
        true
    }
}

/// A stub PolicyEngine that just gathers the current time and hands it off to the StubPolicy as the
/// PolicyData.
#[derive(Debug)]
pub struct StubPolicyEngine<T: TimeSource> {
    time_source: T,
}

impl<T: TimeSource> StubPolicyEngine<T> {
    pub fn new(time_source: T) -> Self {
        Self { time_source }
    }
}

impl<T> PolicyEngine for StubPolicyEngine<T>
where
    T: TimeSource + Clone,
{
    type TimeSource = T;

    fn time_source(&self) -> &Self::TimeSource {
        &self.time_source
    }

    fn compute_next_update_time(
        &mut self,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
    ) -> BoxFuture<'_, CheckTiming> {
        let check_timing = StubPolicy::compute_next_update_time(
            &PolicyData::builder().use_timesource(&self.time_source).build(),
            apps,
            scheduling,
            protocol_state,
        );
        future::ready(check_timing).boxed()
    }

    fn update_check_allowed(
        &mut self,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
        check_options: &CheckOptions,
    ) -> BoxFuture<'_, CheckDecision> {
        let decision = StubPolicy::update_check_allowed(
            &PolicyData::builder().use_timesource(&self.time_source).build(),
            apps,
            scheduling,
            protocol_state,
            check_options,
        );
        future::ready(decision).boxed()
    }

    fn update_can_start<'p>(
        &mut self,
        proposed_install_plan: &'p impl Plan,
    ) -> BoxFuture<'p, UpdateDecision> {
        let decision = StubPolicy::update_can_start(&(), proposed_install_plan);
        future::ready(decision).boxed()
    }

    fn reboot_allowed(&mut self, check_options: &CheckOptions) -> BoxFuture<'_, bool> {
        let decision = StubPolicy::reboot_allowed(&(), check_options);
        future::ready(decision).boxed()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        installer::stub::StubPlan, protocol::request::InstallSource, time::MockTimeSource,
    };

    #[test]
    fn test_compute_next_update_time() {
        let policy_data =
            PolicyData::builder().use_timesource(&MockTimeSource::new_from_now()).build();
        let update_check_schedule = UpdateCheckSchedule::default();
        let result = StubPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &update_check_schedule,
            &ProtocolState::default(),
        );
        let expected = CheckTiming::builder().time(policy_data.current_time).build();
        assert_eq!(result, expected);
    }

    #[test]
    fn test_update_check_allowed_on_demand() {
        let policy_data =
            PolicyData::builder().use_timesource(&MockTimeSource::new_from_now()).build();
        let check_options = CheckOptions { source: InstallSource::OnDemand };
        let result = StubPolicy::update_check_allowed(
            &policy_data,
            &[],
            &UpdateCheckSchedule::default(),
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
        let policy_data =
            PolicyData::builder().use_timesource(&MockTimeSource::new_from_now()).build();
        let check_options = CheckOptions { source: InstallSource::ScheduledTask };
        let result = StubPolicy::update_check_allowed(
            &policy_data,
            &[],
            &UpdateCheckSchedule::default(),
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
        let result = StubPolicy::update_can_start(&(), &StubPlan);
        assert_eq!(result, UpdateDecision::Ok);
    }
}
