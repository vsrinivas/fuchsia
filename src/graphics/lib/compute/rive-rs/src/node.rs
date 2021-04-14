// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    core::{Core, ObjectRef, OnAdded, Property},
    transform_component::TransformComponent,
};

#[derive(Debug, Default)]
pub struct Node {
    transform_component: TransformComponent,
    x: Property<f32>,
    y: Property<f32>,
}

impl ObjectRef<'_, Node> {
    pub fn x(&self) -> f32 {
        self.x.get()
    }

    pub fn set_x(&self, x: f32) {
        if self.x() == x {
            return;
        }

        self.x.set(x);
        ObjectRef::from(&self.transform_component).mark_transform_dirty();
    }

    pub fn y(&self) -> f32 {
        self.y.get()
    }

    pub fn set_y(&self, y: f32) {
        if self.y() == y {
            return;
        }

        self.y.set(y);
        ObjectRef::from(&self.transform_component).mark_transform_dirty();
    }
}

impl Core for Node {
    parent_types![(transform_component, TransformComponent)];

    properties![(13, x, set_x), (14, y, set_y), transform_component];
}

impl OnAdded for ObjectRef<'_, Node> {
    on_added!(TransformComponent);
}
