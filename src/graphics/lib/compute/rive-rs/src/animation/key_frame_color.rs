// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    animation::KeyFrame,
    core::{Core, Object, ObjectRef, OnAdded, Property},
    shapes::paint::Color32,
};

use super::Animator;

#[derive(Debug, Default)]
pub struct KeyFrameColor {
    key_frame: KeyFrame,
    value: Property<Color32>,
}

impl ObjectRef<'_, KeyFrameColor> {
    pub fn value(&self) -> Color32 {
        self.value.get()
    }

    pub fn set_value(&self, value: Color32) {
        self.value.set(value);
    }
}

fn apply_color(core: Object, property_key: u64, mix: f32, value: Color32) {
    let core = core.as_ref();
    let property = core
        .get_property::<Color32>(property_key as u16)
        .expect("KeyFrameColor references wrong property");
    if mix == 1.0 {
        core.animate(&core, property_key, &Animator::new(value));
    } else {
        core.animate(&core, property_key, &Animator::new(property.get().lerp(value, mix)));
    }
}

impl ObjectRef<'_, KeyFrameColor> {
    pub fn apply(&self, core: Object, property_key: u64, mix: f32) {
        apply_color(core, property_key, mix, self.value());
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
        let next_color = next_frame.cast::<KeyFrameColor>();
        let mut f =
            (current_time - key_frame.seconds()) / (next_frame.seconds() - key_frame.seconds());

        if let Some(cubic_interpolator) = key_frame.interpolator() {
            f = cubic_interpolator.as_ref().transform(f);
        }

        apply_color(core, property_key, mix, self.value().lerp(next_color.value(), f));
    }
}

impl Core for KeyFrameColor {
    parent_types![(key_frame, KeyFrame)];

    properties![(88, value, set_value), key_frame];
}

impl OnAdded for ObjectRef<'_, KeyFrameColor> {
    on_added!(KeyFrame);
}
