// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::StateMachineLayerComponent,
    core::{Core, ObjectRef, OnAdded, Property},
};

#[derive(Debug, Default)]
pub struct StateTransition {
    state_machine_layer_component: StateMachineLayerComponent,
    state_to_id: Property<u64>,
    flags: Property<u64>,
    duration: Property<u64>,
}

impl ObjectRef<'_, StateTransition> {
    pub fn state_to_id(&self) -> u64 {
        self.state_to_id.get()
    }

    pub fn set_state_to_id(&self, state_to_id: u64) {
        self.state_to_id.set(state_to_id);
    }

    pub fn flags(&self) -> u64 {
        self.flags.get()
    }

    pub fn set_flags(&self, flags: u64) {
        self.flags.set(flags);
    }

    pub fn duration(&self) -> u64 {
        self.duration.get()
    }

    pub fn set_duration(&self, duration: u64) {
        self.duration.set(duration);
    }
}

impl Core for StateTransition {
    parent_types![(state_machine_layer_component, StateMachineLayerComponent)];

    properties![
        (151, state_to_id, set_state_to_id),
        (152, flags, set_flags),
        (158, duration, set_duration),
        state_machine_layer_component,
    ];
}

impl OnAdded for ObjectRef<'_, StateTransition> {
    on_added!(StateMachineLayerComponent);
}
