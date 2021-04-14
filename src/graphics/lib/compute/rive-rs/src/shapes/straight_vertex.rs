// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    core::{Core, ObjectRef, OnAdded, Property},
    shapes::PathVertex,
};

#[derive(Debug, Default)]
pub struct StraightVertex {
    path_vertex: PathVertex,
    radius: Property<f32>,
}

impl ObjectRef<'_, StraightVertex> {
    pub fn radius(&self) -> f32 {
        self.radius.get()
    }

    pub fn set_radius(&self, radius: f32) {
        if self.radius() == radius {
            return;
        }

        self.radius.set(radius);
        self.cast::<PathVertex>().mark_path_dirty();
    }
}

impl Core for StraightVertex {
    parent_types![(path_vertex, PathVertex)];

    properties![(26, radius, set_radius), path_vertex];
}

impl OnAdded for ObjectRef<'_, StraightVertex> {
    on_added!(PathVertex);
}
