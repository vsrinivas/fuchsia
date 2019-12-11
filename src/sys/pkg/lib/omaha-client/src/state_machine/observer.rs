// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{ProtocolState, UpdateCheckSchedule},
    state_machine::{update_check, State, UpdateCheckError},
};
use futures::future::LocalBoxFuture;
use std::fmt;

/// Observe changes in the state machine.
pub trait Observer {
    fn on_state_change(&mut self, state: State) -> LocalBoxFuture<'_, ()>;

    fn on_schedule_change(&mut self, schedule: &UpdateCheckSchedule) -> LocalBoxFuture<'_, ()>;

    fn on_protocol_state_change(
        &mut self,
        protocol_state: &ProtocolState,
    ) -> LocalBoxFuture<'_, ()>;

    fn on_update_check_result(
        &mut self,
        result: &Result<update_check::Response, UpdateCheckError>,
    ) -> LocalBoxFuture<'_, ()>;
}

impl fmt::Debug for dyn Observer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Observer")
    }
}
