// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{App, CheckOptions, ProtocolState, UpdateCheckSchedule},
    installer::Plan,
    policy::{CheckDecision, PolicyEngine, UpdateDecision},
};
use futures::future::BoxFuture;
use futures::prelude::*;

/// A mock PolicyEngine that returns mocked data.
#[derive(Debug, Default)]
pub struct MockPolicyEngine {
    pub check_schedule: UpdateCheckSchedule,
    pub check_decision: CheckDecision,
    pub update_decision: UpdateDecision,
}

impl PolicyEngine for MockPolicyEngine {
    fn compute_next_update_time(
        &mut self,
        _apps: &[App],
        _scheduling: &UpdateCheckSchedule,
        _protocol_state: &ProtocolState,
    ) -> BoxFuture<'_, UpdateCheckSchedule> {
        future::ready(self.check_schedule.clone()).boxed()
    }

    fn update_check_allowed(
        &mut self,
        _apps: &[App],
        _scheduling: &UpdateCheckSchedule,
        _protocol_state: &ProtocolState,
        _check_options: &CheckOptions,
    ) -> BoxFuture<'_, CheckDecision> {
        future::ready(self.check_decision.clone()).boxed()
    }

    fn update_can_start(
        &mut self,
        _proposed_install_plan: &impl Plan,
    ) -> BoxFuture<'_, UpdateDecision> {
        future::ready(self.update_decision.clone()).boxed()
    }
}
