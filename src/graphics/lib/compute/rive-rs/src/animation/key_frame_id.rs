// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::KeyFrame,
    core::{Core, Object, ObjectRef, OnAdded, Property},
};

use super::Animator;

#[derive(Debug, Default)]
pub struct KeyFrameId {
    key_frame: KeyFrame,
    value: Property<u64>,
}

impl ObjectRef<'_, KeyFrameId> {
    pub fn value(&self) -> u64 {
        self.value.get()
    }

    pub fn set_value(&self, value: u64) {
        self.value.set(value);
    }
}

impl ObjectRef<'_, KeyFrameId> {
    pub fn apply(&self, core: Object, property_key: u64, _mix: f32) {
        core.as_ref().animate(&core.as_ref(), property_key, &Animator::new(self.value()));
    }

    pub fn apply_interpolation(
        &self,
        core: Object,
        property_key: u64,
        _current_time: f32,
        _next_frame: ObjectRef<'_, KeyFrame>,
        _mix: f32,
    ) {
        core.as_ref().animate(&core.as_ref(), property_key, &Animator::new(self.value()));
    }
}

impl Core for KeyFrameId {
    parent_types![(key_frame, KeyFrame)];

    properties![(122, value, set_value), key_frame];
}

impl OnAdded for ObjectRef<'_, KeyFrameId> {
    on_added!(KeyFrame);
}
