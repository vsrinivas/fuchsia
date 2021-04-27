// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::Cell;

use crate::{
    animation::StateMachineInput,
    core::{Core, ObjectRef, OnAdded},
};

#[derive(Debug, Default)]
pub struct StateMachineTrigger {
    state_machine_input: StateMachineInput,
    is_fired: Cell<bool>,
}

impl ObjectRef<'_, StateMachineTrigger> {
    pub fn reset(&self) {
        self.is_fired.set(false);
    }

    pub fn fire(&self) {
        self.is_fired.set(true);
    }
}

impl Core for StateMachineTrigger {
    parent_types![(state_machine_input, StateMachineInput)];

    properties!(state_machine_input);
}

impl OnAdded for ObjectRef<'_, StateMachineTrigger> {
    on_added!(StateMachineInput);
}
