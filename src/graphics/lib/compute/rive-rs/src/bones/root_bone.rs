// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    bones::Bone,
    core::{Core, CoreContext, ObjectRef, OnAdded, Property},
    status_code::StatusCode,
    transform_component::TransformComponent,
};

#[derive(Debug, Default)]
pub struct RootBone {
    bone: Bone,
    x: Property<f32>,
    y: Property<f32>,
}

impl ObjectRef<'_, RootBone> {
    pub fn x(&self) -> f32 {
        self.x.get()
    }

    pub fn set_x(&self, x: f32) {
        if self.x() == x {
            return;
        }

        self.x.set(x);
        self.cast::<TransformComponent>().mark_transform_dirty();
    }

    pub fn y(&self) -> f32 {
        self.y.get()
    }

    pub fn set_y(&self, y: f32) {
        if self.y() == y {
            return;
        }

        self.y.set(y);
        self.cast::<TransformComponent>().mark_transform_dirty();
    }
}

impl Core for RootBone {
    parent_types![(bone, Bone)];

    properties![(90, x, set_x), (91, y, set_y), bone];
}

impl OnAdded for ObjectRef<'_, RootBone> {
    on_added!([on_added_dirty, import], Bone);

    fn on_added_clean(&self, context: &dyn CoreContext) -> StatusCode {
        // Intentionally doesn't call Bone::on_added_clean and goes straight to
        // the super.super TransformComponent as that assumes the parent must be a
        // Bone while a root bone is a special case Bone that can be parented to
        // other TransformComponents.
        self.cast::<TransformComponent>().on_added_clean(context)
    }
}
