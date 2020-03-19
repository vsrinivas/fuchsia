// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{ProtocolState, UpdateCheckSchedule},
    state_machine::{update_check, State, UpdateCheckError},
};

/// Events emitted by the state machine.
#[derive(Debug)]
pub enum StateMachineEvent {
    StateChange(State),
    ScheduleChange(UpdateCheckSchedule),
    ProtocolStateChange(ProtocolState),
    UpdateCheckResult(Result<update_check::Response, UpdateCheckError>),
}
