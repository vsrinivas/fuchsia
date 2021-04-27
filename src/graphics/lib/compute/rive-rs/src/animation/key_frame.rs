// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{any::TypeId, cell::Cell};

use crate::{
    animation::{CubicInterpolator, KeyFrameColor, KeyFrameDouble, KeyFrameId, KeyedProperty},
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    importers::{ImportStack, KeyedPropertyImporter},
    option_cell::OptionCell,
    status_code::StatusCode,
};

#[derive(Debug, Default)]
pub struct KeyFrame {
    frame: Property<u64>,
    interpolation_type: Property<u64>,
    interpolator_id: Property<u64>,
    cubic_interpolator: OptionCell<Object<CubicInterpolator>>,
    seconds: Cell<f32>,
}

impl ObjectRef<'_, KeyFrame> {
    pub fn frame(&self) -> u64 {
        self.frame.get()
    }

    pub fn set_frame(&self, frame: u64) {
        self.frame.set(frame);
    }

    pub fn interpolation_type(&self) -> u64 {
        self.interpolation_type.get()
    }

    pub fn set_interpolation_type(&self, interpolation_type: u64) {
        self.interpolation_type.set(interpolation_type);
    }

    pub fn interpolator_id(&self) -> u64 {
        self.interpolator_id.get()
    }

    pub fn set_interpolator_id(&self, interpolator_id: u64) {
        self.interpolator_id.set(interpolator_id);
    }
}

impl ObjectRef<'_, KeyFrame> {
    pub fn seconds(&self) -> f32 {
        self.seconds.get()
    }

    pub fn interpolator(&self) -> Option<Object<CubicInterpolator>> {
        self.cubic_interpolator.get()
    }

    pub fn compute_seconds(&self, fps: u64) {
        self.seconds.set(self.frame() as f32 / fps as f32);
    }

    pub fn apply(&self, core: Object, property_key: u64, mix: f32) {
        if let Some(key_frame_color) = self.try_cast::<KeyFrameColor>() {
            return key_frame_color.apply(core, property_key, mix);
        }

        if let Some(key_frame_double) = self.try_cast::<KeyFrameDouble>() {
            return key_frame_double.apply(core, property_key, mix);
        }

        if let Some(key_frame_id) = self.try_cast::<KeyFrameId>() {
            return key_frame_id.apply(core, property_key, mix);
        }

        unreachable!();
    }

    pub fn apply_interpolation(
        &self,
        core: Object,
        property_key: u64,
        seconds: f32,
        next_frame: ObjectRef<'_, KeyFrame>,
        mix: f32,
    ) {
        if let Some(key_frame_color) = self.try_cast::<KeyFrameColor>() {
            return key_frame_color.apply_interpolation(
                core,
                property_key,
                seconds,
                next_frame,
                mix,
            );
        }

        if let Some(key_frame_double) = self.try_cast::<KeyFrameDouble>() {
            return key_frame_double.apply_interpolation(
                core,
                property_key,
                seconds,
                next_frame,
                mix,
            );
        }

        if let Some(key_frame_id) = self.try_cast::<KeyFrameId>() {
            return key_frame_id.apply_interpolation(core, property_key, seconds, next_frame, mix);
        }

        unreachable!();
    }
}

impl Core for KeyFrame {
    properties![
        (67, frame, set_frame),
        (68, interpolation_type, set_interpolation_type),
        (69, interpolator_id, set_interpolator_id)
    ];
}

impl OnAdded for ObjectRef<'_, KeyFrame> {
    on_added!([on_added_clean]);

    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        if self.interpolator_id() != 0 {
            if let Some(cubic_interpolator) =
                context.resolve(self.interpolator_id() as usize).and_then(|core| core.try_cast())
            {
                self.cubic_interpolator.set(Some(cubic_interpolator));
            } else {
                return StatusCode::MissingObject;
            }
        }

        StatusCode::Ok
    }

    fn import(&self, object: Object, import_stack: &ImportStack) -> StatusCode {
        if let Some(importer) =
            import_stack.latest::<KeyedPropertyImporter>(TypeId::of::<KeyedProperty>())
        {
            importer.push_key_frame(object.as_ref().cast::<KeyFrame>().as_object());
            StatusCode::Ok
        } else {
            StatusCode::MissingObject
        }
    }
}
