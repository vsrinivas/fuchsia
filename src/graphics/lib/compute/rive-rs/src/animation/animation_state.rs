// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::LayerState,
    core::{Core, ObjectRef, OnAdded, Property},
};

#[derive(Debug, Default)]
pub struct AnimationState {
    layer_state: LayerState,
    animation_id: Property<u64>,
}

impl ObjectRef<'_, AnimationState> {
    pub fn animation_id(&self) -> u64 {
        self.animation_id.get()
    }

    pub fn set_animation_id(&self, animation_id: u64) {
        self.animation_id.set(animation_id);
    }
}

impl Core for AnimationState {
    parent_types![(layer_state, LayerState)];

    properties![(149, animation_id, set_animation_id), layer_state];
}

impl OnAdded for ObjectRef<'_, AnimationState> {
    on_added!(LayerState);
}
