// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::StateMachineInput,
    core::{Core, ObjectRef, OnAdded, Property},
};

#[derive(Debug, Default)]
pub struct StateMachineBool {
    state_machine_input: StateMachineInput,
    value: Property<bool>,
}

impl ObjectRef<'_, StateMachineBool> {
    pub fn value(&self) -> bool {
        self.value.get()
    }

    pub fn set_value(&self, value: bool) {
        self.value.set(value);
    }
}

impl Core for StateMachineBool {
    parent_types![(state_machine_input, StateMachineInput)];

    properties![(141, value, set_value), state_machine_input];
}

impl OnAdded for ObjectRef<'_, StateMachineBool> {
    on_added!(StateMachineInput);
}
