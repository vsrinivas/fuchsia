// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::StateMachineComponent,
    core::{Core, ObjectRef, OnAdded},
};

#[derive(Debug, Default)]
pub struct StateMachineLayer {
    state_machine_component: StateMachineComponent,
}

impl Core for StateMachineLayer {
    parent_types![(state_machine_component, StateMachineComponent)];

    properties!(state_machine_component);
}

impl OnAdded for ObjectRef<'_, StateMachineLayer> {
    on_added!(StateMachineComponent);
}
