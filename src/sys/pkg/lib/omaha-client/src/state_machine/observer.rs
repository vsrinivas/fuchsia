// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{ProtocolState, UpdateCheckSchedule},
    installer::ProgressObserver,
    state_machine::{update_check, State, UpdateCheckError},
};
use futures::{channel::mpsc, future::BoxFuture, prelude::*};

/// Events emitted by the state machine.
#[derive(Debug)]
pub enum StateMachineEvent {
    StateChange(State),
    ScheduleChange(UpdateCheckSchedule),
    ProtocolStateChange(ProtocolState),
    UpdateCheckResult(Result<update_check::Response, UpdateCheckError>),
    InstallProgressChange(InstallProgress),
}

#[derive(Debug)]
pub struct InstallProgress {
    pub progress: f32,
}

pub(super) struct StateMachineProgressObserver(pub(super) mpsc::Sender<InstallProgress>);

impl ProgressObserver for StateMachineProgressObserver {
    fn receive_progress(
        &self,
        _operation: Option<&str>,
        progress: f32,
        _total_size: Option<u64>,
        _size_so_far: Option<u64>,
    ) -> BoxFuture<'_, ()> {
        async move {
            let _ = self.0.clone().send(InstallProgress { progress }).await;
        }
        .boxed()
    }
}
