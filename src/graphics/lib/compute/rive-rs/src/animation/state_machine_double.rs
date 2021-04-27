// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::StateMachineInput,
    core::{Core, ObjectRef, OnAdded, Property},
};

#[derive(Debug, Default)]
pub struct StateMachineDouble {
    state_machine_input: StateMachineInput,
    value: Property<f32>,
}

impl ObjectRef<'_, StateMachineDouble> {
    pub fn value(&self) -> f32 {
        self.value.get()
    }

    pub fn set_value(&self, value: f32) {
        self.value.set(value);
    }
}

impl Core for StateMachineDouble {
    parent_types![(state_machine_input, StateMachineInput)];

    properties![(140, value, set_value), state_machine_input];
}

impl OnAdded for ObjectRef<'_, StateMachineDouble> {
    on_added!(StateMachineInput);
}
