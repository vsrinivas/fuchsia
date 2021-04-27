// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::KeyFrame,
    core::{Core, Object, ObjectRef, OnAdded, Property},
    math,
};

use super::Animator;

#[derive(Debug, Default)]
pub struct KeyFrameDouble {
    key_frame: KeyFrame,
    value: Property<f32>,
}

impl ObjectRef<'_, KeyFrameDouble> {
    pub fn value(&self) -> f32 {
        self.value.get()
    }

    pub fn set_value(&self, value: f32) {
        self.value.set(value);
    }
}

fn apply_double(core: Object, property_key: u64, mix: f32, value: f32) {
    let core = core.as_ref();
    let property = core
        .get_property::<f32>(property_key as u16)
        .expect("KeyFrameDouble references wrong property");
    if mix == 1.0 {
        core.animate(&core, property_key, &Animator::new(value));
    } else {
        core.animate(&core, property_key, &Animator::new(math::lerp(property.get(), value, mix)));
    }
}

impl ObjectRef<'_, KeyFrameDouble> {
    pub fn apply(&self, core: Object, property_key: u64, mix: f32) {
        apply_double(core, property_key, mix, self.value());
    }

    pub fn apply_interpolation(
        &self,
        core: Object,
        property_key: u64,
        current_time: f32,
        next_frame: ObjectRef<'_, KeyFrame>,
        mix: f32,
    ) {
        let key_frame = self.cast::<KeyFrame>();
        let next_double = next_frame.cast::<KeyFrameDouble>();
        let mut f =
            (current_time - key_frame.seconds()) / (next_frame.seconds() - key_frame.seconds());

        if let Some(cubic_interpolator) = key_frame.interpolator() {
            f = cubic_interpolator.as_ref().transform(f);
        }

        apply_double(core, property_key, mix, math::lerp(self.value(), next_double.value(), f));
    }
}

impl Core for KeyFrameDouble {
    parent_types![(key_frame, KeyFrame)];

    properties![(70, value, set_value), key_frame];
}

impl OnAdded for ObjectRef<'_, KeyFrameDouble> {
    on_added!(KeyFrame);
}
