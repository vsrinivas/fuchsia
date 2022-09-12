// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{App, CheckOptions, CheckTiming, ProtocolState, UpdateCheckSchedule},
    installer::Plan,
    request_builder::RequestParams,
    time::{ComplexTime, TimeSource},
};
use futures::future::BoxFuture;

#[cfg(test)]
mod mock;
#[cfg(test)]
pub use mock::MockPolicyEngine;
mod stub;
pub use stub::StubPolicy;
pub use stub::StubPolicyEngine;
use typed_builder::TypedBuilder;

/// Data about the local system that's needed to fulfill Policy questions
#[derive(Clone, Debug, TypedBuilder)]
pub struct PolicyData {
    /// The current time at the start of the update
    #[builder(setter(into))]
    pub current_time: ComplexTime,
}

/// Reasons why a check can/cannot be performed at this time
#[derive(Clone, Debug, PartialEq)]
pub enum CheckDecision {
    /// positive responses
    Ok(RequestParams),
    /// but with caveats:
    OkUpdateDeferred(RequestParams),

    /// negative responses
    TooSoon,
    ThrottledByPolicy,
    DeniedByPolicy,
}

#[cfg(test)]
impl Default for CheckDecision {
    fn default() -> Self {
        CheckDecision::Ok(RequestParams::default())
    }
}

/// Reasons why an update can/cannot be performed at this time
#[derive(Clone, Debug, PartialEq)]
pub enum UpdateDecision {
    /// Update can be performed.
    Ok,
    /// Update is deferred by Policy.
    DeferredByPolicy,
    /// Update is rejected by Policy.
    DeniedByPolicy,
}

#[cfg(test)]
impl Default for UpdateDecision {
    fn default() -> Self {
        UpdateDecision::Ok
    }
}

/// The policy implementation itself
pub trait Policy {
    type ComputeNextUpdateTimePolicyData;
    type UpdateCheckAllowedPolicyData;
    type UpdateCanStartPolicyData;
    type RebootPolicyData;
    type InstallPlan: Plan;

    /// When should the next update happen?
    fn compute_next_update_time(
        policy_data: &Self::ComputeNextUpdateTimePolicyData,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
    ) -> CheckTiming;

    /// Given the current State, and the current PolicyData, is an update check
    /// allowed at this time.  A CheckDecision is used to return the reasoning, as in
    /// some cases, instead of an update check, the SM will instead notify Omaha that
    /// it would perform an update, but instead just tell the device whether or not
    /// an update is available.
    fn update_check_allowed(
        policy_data: &Self::UpdateCheckAllowedPolicyData,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
        check_options: &CheckOptions,
    ) -> CheckDecision;

    /// Given the current State, the current PolicyData, can the proposed InstallPlan
    /// be executed at this time.
    fn update_can_start(
        policy_data: &Self::UpdateCanStartPolicyData,
        proposed_install_plan: &Self::InstallPlan,
    ) -> UpdateDecision;

    /// Given the current PolicyData, is reboot allowed right now.
    fn reboot_allowed(policy_data: &Self::RebootPolicyData, check_options: &CheckOptions) -> bool;

    /// Given the InstallPlan, is reboot needed after update has been installed.
    fn reboot_needed(install_plan: &Self::InstallPlan) -> bool;
}

pub trait PolicyEngine {
    type TimeSource: TimeSource + Clone;
    type InstallResult;
    type InstallPlan: Plan;

    /// Provides the time source used by the PolicyEngine to the state machine.
    fn time_source(&self) -> &Self::TimeSource;

    /// When should the next update happen?
    fn compute_next_update_time<'a>(
        &'a mut self,
        apps: &'a [App],
        scheduling: &'a UpdateCheckSchedule,
        protocol_state: &'a ProtocolState,
    ) -> BoxFuture<'a, CheckTiming>;

    /// Given the context provided by State, does the Policy allow an update check to
    /// happen at this time?  This should be consistent with the compute_next_update_time
    /// so that during background updates, the result of compute_next_update_time will
    /// result in a CheckDecision::Ok() value from this function.
    fn update_check_allowed<'a>(
        &'a mut self,
        apps: &'a [App],
        scheduling: &'a UpdateCheckSchedule,
        protocol_state: &'a ProtocolState,
        check_options: &'a CheckOptions,
    ) -> BoxFuture<'a, CheckDecision>;

    /// Given the current State, the current PolicyData, can the proposed InstallPlan
    /// be executed at this time.
    fn update_can_start<'a>(
        &'a mut self,
        proposed_install_plan: &'a Self::InstallPlan,
    ) -> BoxFuture<'a, UpdateDecision>;

    /// Is reboot allowed right now.
    fn reboot_allowed<'a>(
        &'a mut self,
        check_options: &'a CheckOptions,
        install_result: &'a Self::InstallResult,
    ) -> BoxFuture<'a, bool>;

    /// Given the InstallPlan, is reboot needed after update has been installed.
    fn reboot_needed<'a>(&'a mut self, install_plan: &'a Self::InstallPlan) -> BoxFuture<'a, bool>;
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::time::MockTimeSource;

    #[test]
    pub fn test_policy_data_builder_with_system_time() {
        let current_time = MockTimeSource::new_from_now().now();
        let policy_data = PolicyData::builder().current_time(current_time).build();
        assert_eq!(policy_data.current_time, current_time);
    }

    #[test]
    pub fn test_policy_data_builder_with_clock() {
        let source = MockTimeSource::new_from_now();
        let current_time = source.now();
        let policy_data = PolicyData::builder().current_time(source.now()).build();
        assert_eq!(policy_data.current_time, current_time);
    }
}
