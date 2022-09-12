// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::install_plan::FuchsiaInstallPlan;
use futures::future::BoxFuture;
use futures::prelude::*;
use omaha_client::{
    common::{App, CheckOptions, CheckTiming, ProtocolState, UpdateCheckSchedule},
    policy::{CheckDecision, Policy, PolicyData, PolicyEngine, UpdateDecision},
    request_builder::RequestParams,
    time::TimeSource,
};

/// The Policy implementation for isolated SWD.
pub struct IsolatedPolicy;

impl Policy for IsolatedPolicy {
    type ComputeNextUpdateTimePolicyData = PolicyData;
    type UpdateCheckAllowedPolicyData = ();
    type UpdateCanStartPolicyData = ();
    type RebootPolicyData = ();
    type InstallPlan = FuchsiaInstallPlan;

    fn compute_next_update_time(
        policy_data: &Self::ComputeNextUpdateTimePolicyData,
        _apps: &[App],
        _scheduling: &UpdateCheckSchedule,
        _protocol_state: &ProtocolState,
    ) -> CheckTiming {
        CheckTiming::builder().time(policy_data.current_time).build()
    }

    fn update_check_allowed(
        _policy_data: &Self::UpdateCheckAllowedPolicyData,
        _apps: &[App],
        _scheduling: &UpdateCheckSchedule,
        _protocol_state: &ProtocolState,
        check_options: &CheckOptions,
    ) -> CheckDecision {
        CheckDecision::Ok(RequestParams {
            source: check_options.source,
            use_configured_proxies: true,
            disable_updates: false,
            offer_update_if_same_version: true,
        })
    }

    fn update_can_start(
        _policy_data: &Self::UpdateCanStartPolicyData,
        _proposed_install_plan: &Self::InstallPlan,
    ) -> UpdateDecision {
        UpdateDecision::Ok
    }

    fn reboot_allowed(
        _policy_data: &Self::RebootPolicyData,
        _check_options: &CheckOptions,
    ) -> bool {
        true
    }

    fn reboot_needed(_install_plan: &Self::InstallPlan) -> bool {
        true
    }
}

/// The Policy implementation for isolated SWD that just gathers the current time and hands it off
/// to the IsolatedPolicy as the PolicyData.
#[derive(Debug)]
pub struct IsolatedPolicyEngine<T: TimeSource> {
    time_source: T,
}

impl<T> IsolatedPolicyEngine<T>
where
    T: TimeSource,
{
    pub fn new(time_source: T) -> Self {
        Self { time_source }
    }
}

impl<T> PolicyEngine for IsolatedPolicyEngine<T>
where
    T: TimeSource + Clone,
{
    type TimeSource = T;
    type InstallResult = ();
    type InstallPlan = FuchsiaInstallPlan;

    fn time_source(&self) -> &Self::TimeSource {
        &self.time_source
    }

    fn compute_next_update_time(
        &mut self,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
    ) -> BoxFuture<'_, CheckTiming> {
        let check_timing = IsolatedPolicy::compute_next_update_time(
            &PolicyData::builder().current_time(self.time_source.now()).build(),
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
        let decision = IsolatedPolicy::update_check_allowed(
            &(),
            apps,
            scheduling,
            protocol_state,
            check_options,
        );
        future::ready(decision).boxed()
    }

    fn update_can_start<'p>(
        &mut self,
        proposed_install_plan: &'p Self::InstallPlan,
    ) -> BoxFuture<'p, UpdateDecision> {
        let decision = IsolatedPolicy::update_can_start(&(), proposed_install_plan);
        future::ready(decision).boxed()
    }

    fn reboot_allowed(
        &mut self,
        check_options: &CheckOptions,
        _install_result: &Self::InstallResult,
    ) -> BoxFuture<'_, bool> {
        let decision = IsolatedPolicy::reboot_allowed(&(), check_options);
        future::ready(decision).boxed()
    }

    fn reboot_needed(&mut self, install_plan: &Self::InstallPlan) -> BoxFuture<'_, bool> {
        let decision = IsolatedPolicy::reboot_needed(install_plan);
        future::ready(decision).boxed()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_url::PinnedAbsolutePackageUrl;
    use omaha_client::{protocol::request::InstallSource, time::MockTimeSource};

    #[test]
    fn test_compute_next_update_time() {
        let policy_data =
            PolicyData::builder().current_time(MockTimeSource::new_from_now().now()).build();
        let update_check_schedule = UpdateCheckSchedule::default();
        let result = IsolatedPolicy::compute_next_update_time(
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
        let check_options = CheckOptions { source: InstallSource::OnDemand };
        let result = IsolatedPolicy::update_check_allowed(
            &(),
            &[],
            &UpdateCheckSchedule::default(),
            &ProtocolState::default(),
            &check_options,
        );
        let expected = CheckDecision::Ok(RequestParams {
            source: check_options.source,
            use_configured_proxies: true,
            offer_update_if_same_version: true,
            ..RequestParams::default()
        });
        assert_eq!(result, expected);
    }

    #[test]
    fn test_update_check_allowed_scheduled_task() {
        let check_options = CheckOptions { source: InstallSource::ScheduledTask };
        let result = IsolatedPolicy::update_check_allowed(
            &(),
            &[],
            &UpdateCheckSchedule::default(),
            &ProtocolState::default(),
            &check_options,
        );
        let expected = CheckDecision::Ok(RequestParams {
            source: check_options.source,
            use_configured_proxies: true,
            offer_update_if_same_version: true,
            ..RequestParams::default()
        });
        assert_eq!(result, expected);
    }

    #[test]
    fn test_update_can_start() {
        const TEST_URL: &str = "fuchsia-pkg://fuchsia.com/update/0?hash=0000000000000000000000000000000000000000000000000000000000000000";
        let install_plan = FuchsiaInstallPlan {
            url: PinnedAbsolutePackageUrl::parse(TEST_URL).unwrap(),
            install_source: InstallSource::ScheduledTask,
        };

        let result = IsolatedPolicy::update_can_start(&(), &install_plan);
        assert_eq!(result, UpdateDecision::Ok);
    }
}
