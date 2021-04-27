// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::StateMachineLayerComponent,
    core::{Core, ObjectRef, OnAdded},
};

#[derive(Debug, Default)]
pub struct LayerState {
    state_machine_layer_component: StateMachineLayerComponent,
}

impl Core for LayerState {
    parent_types![(state_machine_layer_component, StateMachineLayerComponent)];

    properties!(state_machine_layer_component);
}

impl OnAdded for ObjectRef<'_, LayerState> {
    on_added!(StateMachineLayerComponent);
}
