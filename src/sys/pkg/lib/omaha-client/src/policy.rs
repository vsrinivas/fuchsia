// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    clock,
    common::{format_system_time, App, CheckOptions, ProtocolState, UpdateCheckSchedule},
    installer::Plan,
    request_builder::RequestParams,
};
use futures::future::BoxFuture;
use std::time::SystemTime;

#[cfg(test)]
mod mock;
#[cfg(test)]
pub use mock::MockPolicyEngine;
mod stub;
pub use stub::StubPolicy;
pub use stub::StubPolicyEngine;

/// Data about the local system that's needed to fulfill Policy questions
pub struct PolicyData {
    /// The current time at the start of the update
    pub current_time: SystemTime,
}

impl PolicyData {
    /// Create and return a new builder for PolicyData.    
    pub fn builder() -> PolicyDataBuilder {
        PolicyDataBuilder::default()
    }
}

impl std::fmt::Debug for PolicyData {
    /// Implement Debug such that it uses the same time formatting as
    /// crate::common::UpdateCheckSchedule uses.
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PolicyData")
            .field("current_time", &format_system_time(self.current_time))
            .finish()
    }
}

/// The PolicyDataBuilder uses the typestate pattern.  The builder cannot be built until the time
/// has been specified (which changes the type of the builder).
#[derive(Debug, Default)]
pub struct PolicyDataBuilder;

/// The PolicyDataBuilder, once it has time set.
pub struct PolicyDataBuilderWithTime {
    current_time: SystemTime,
}

impl PolicyDataBuilder {
    /// Use |crate::clock| to set the |current_time|.
    pub fn use_clock(self) -> PolicyDataBuilderWithTime {
        PolicyDataBuilderWithTime { current_time: clock::now() }
    }

    /// Set the |current_time| explicitly from a given SystemTime.
    pub fn time(self, current_time: SystemTime) -> PolicyDataBuilderWithTime {
        PolicyDataBuilderWithTime { current_time }
    }
}

/// These are the operations that can be performed once the time has been set.
impl PolicyDataBuilderWithTime {
    /// Construct the PolicyData
    pub fn build(self) -> PolicyData {
        PolicyData { current_time: self.current_time }
    }
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
    /// When should the next update happen?
    fn compute_next_update_time(
        policy_data: &PolicyData,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
    ) -> UpdateCheckSchedule;

    /// Given the current State, and the current PolicyData, is an update check
    /// allowed at this time.  A CheckDecision is used to return the reasoning, as in
    /// some cases, instead of an update check, the SM will instead notify Omaha that
    /// it would perform an update, but instead just tell the device whether or not
    /// an update is available.
    fn update_check_allowed(
        policy_data: &PolicyData,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
        check_options: &CheckOptions,
    ) -> CheckDecision;

    /// Given the current State, the current PolicyData, can the proposed InstallPlan
    /// be executed at this time.
    fn update_can_start(
        policy_data: &PolicyData,
        proposed_install_plan: &impl Plan,
    ) -> UpdateDecision;
}

pub trait PolicyEngine {
    /// When should the next update happen?
    fn compute_next_update_time(
        &mut self,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
    ) -> BoxFuture<'_, UpdateCheckSchedule>;

    /// Given the context provided by State, does the Policy allow an update check to
    /// happen at this time?  This should be consistent with the compute_next_update_time
    /// so that during background updates, the result of compute_next_update_time will
    /// result in a CheckDecision::Ok() value from this function.
    fn update_check_allowed(
        &mut self,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
        check_options: &CheckOptions,
    ) -> BoxFuture<'_, CheckDecision>;

    /// Given the current State, the current PolicyData, can the proposed InstallPlan
    /// be executed at this time.
    fn update_can_start(
        &mut self,
        proposed_install_plan: &impl Plan,
    ) -> BoxFuture<'_, UpdateDecision>;
}

#[cfg(test)]
mod test {
    use super::*;
    use std::time::Duration;

    #[test]
    pub fn test_policy_data_builder_with_system_time() {
        let current_time = SystemTime::UNIX_EPOCH + Duration::from_secs(200000);
        let policy_data = PolicyData::builder().time(current_time).build();
        assert_eq!(policy_data.current_time, current_time);
    }

    #[test]
    pub fn test_policy_data_builder_with_clock() {
        let current_time = clock::now();
        let policy_data = PolicyData::builder().use_clock().build();
        assert_eq!(policy_data.current_time, current_time);
    }
}
