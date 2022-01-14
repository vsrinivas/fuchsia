// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::Cell;

use crate::{
    component::Component,
    core::{Core, CoreContext, ObjectRef, OnAdded, Property},
    math::{self, Mat},
    shapes::PathVertex,
    status_code::StatusCode,
};

#[derive(Debug)]
pub struct Weight {
    component: Component,
    values: Property<u64>,
    indices: Property<u64>,
    translation: Cell<math::Vec>,
}

impl ObjectRef<'_, Weight> {
    pub fn values(&self) -> u64 {
        self.values.get()
    }

    pub fn set_values(&self, values: u64) {
        self.values.set(values);
    }

    pub fn indices(&self) -> u64 {
        self.indices.get()
    }

    pub fn set_indices(&self, indices: u64) {
        self.indices.set(indices);
    }
}

impl ObjectRef<'_, Weight> {
    pub fn translation(&self) -> math::Vec {
        self.translation.get()
    }

    pub fn set_translation(&self, translation: math::Vec) {
        self.translation.set(translation);
    }
}

impl Weight {
    pub fn deform(
        x: f32,
        y: f32,
        indices: usize,
        weights: usize,
        world: Mat,
        bone_transforms: &[Cell<Mat>],
    ) -> math::Vec {
        let mut deformation = Mat::zero();
        let r = world * math::Vec::new(x, y);

        let encoded_weight_value = |index, data| (data >> (index * 8)) & 0xFF;

        for i in 0usize..4 {
            let weight = encoded_weight_value(i, weights);
            if weight == 0 {
                continue;
            }

            let normalized_weight = weight as f32 / 255.0;
            let index = encoded_weight_value(i, indices);

            let transform = bone_transforms[index].get();

            deformation.scale_x += transform.scale_x * normalized_weight;
            deformation.shear_y += transform.shear_y * normalized_weight;
            deformation.shear_x += transform.shear_x * normalized_weight;
            deformation.scale_y += transform.scale_y * normalized_weight;
            deformation.translate_x += transform.translate_x * normalized_weight;
            deformation.translate_y += transform.translate_y * normalized_weight;
        }

        deformation * r
    }
}

impl Core for Weight {
    parent_types![(component, Component)];

    properties![(102, values, set_values), (103, indices, set_indices), component];
}

impl OnAdded for ObjectRef<'_, Weight> {
    on_added!([on_added_clean, import], Component);

    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        let component = self.cast::<Component>();

        let code = component.on_added_dirty(context);
        if code != StatusCode::Ok {
            return code;
        }

        if let Some(parent) = component.parent().and_then(|core| core.try_cast::<PathVertex>()) {
            parent.as_ref().set_weight(self.as_object());
        } else {
            return StatusCode::MissingObject;
        }

        StatusCode::Ok
    }
}

impl Default for Weight {
    fn default() -> Self {
        Self {
            component: Component::default(),
            values: Property::new(255),
            indices: Property::new(1),
            translation: Cell::new(math::Vec::default()),
        }
    }
}
