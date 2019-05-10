// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{App, CheckOptions, ProtocolState, UpdateCheckSchedule},
    installer::Plan,
    request_builder::RequestParams,
};
use std::time::SystemTime;

pub mod stub;
pub use stub::StubPolicy;

/// Data about the local system that's needed to fulfill Policy questions
pub struct PolicyData {
    /// The current time at the start of the update
    pub current_time: SystemTime,
}

/// Reasons why a check can/cannot be performed at this time
#[derive(Debug, PartialEq)]
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

/// Reasons why an update can/cannot be performed at this time
#[derive(Debug, PartialEq)]
pub enum UpdateDecision {
    /// Update can be performed.
    Ok,
    /// Update is deferred by Policy.
    DeferredByPolicy,
    /// Update is rejected by Policy.
    DeniedByPolicy,
}

/// The policy implementation itself
trait Policy {
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
