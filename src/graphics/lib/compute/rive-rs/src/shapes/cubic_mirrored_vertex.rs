// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    core::{Core, ObjectRef, OnAdded, Property},
    math,
    shapes::{CubicVertex, PathVertex},
};

#[derive(Debug, Default)]
pub struct CubicMirroredVertex {
    cubic_vertex: CubicVertex,
    rotation: Property<f32>,
    distance: Property<f32>,
}

impl ObjectRef<'_, CubicMirroredVertex> {
    pub fn rotation(&self) -> f32 {
        self.rotation.get()
    }

    pub fn set_rotation(&self, rotation: f32) {
        if self.rotation() == rotation {
            return;
        }

        self.rotation.set(rotation);

        self.cast::<CubicVertex>().invalidate_in();
        self.cast::<CubicVertex>().invalidate_out();
        self.cast::<PathVertex>().mark_path_dirty();
    }

    pub fn distance(&self) -> f32 {
        self.distance.get()
    }

    pub fn set_distance(&self, distance: f32) {
        if self.distance() == distance {
            return;
        }

        self.distance.set(distance);

        self.cast::<CubicVertex>().invalidate_in();
        self.cast::<CubicVertex>().invalidate_out();
        self.cast::<PathVertex>().mark_path_dirty();
    }
}

impl ObjectRef<'_, CubicMirroredVertex> {
    pub fn compute_in(&self) {
        let pos = math::Vec::new(self.cast::<PathVertex>().x(), self.cast::<PathVertex>().y());
        let diff = math::Vec::new(
            self.rotation().cos() * -self.distance(),
            self.rotation().sin() * -self.distance(),
        );

        self.cast::<CubicVertex>().set_in_point(pos + diff);
    }

    pub fn compute_out(&self) {
        let pos = math::Vec::new(self.cast::<PathVertex>().x(), self.cast::<PathVertex>().y());
        let diff = math::Vec::new(
            self.rotation().cos() * self.distance(),
            self.rotation().sin() * self.distance(),
        );

        self.cast::<CubicVertex>().set_out_point(pos + diff);
    }
}

impl Core for CubicMirroredVertex {
    parent_types![(cubic_vertex, CubicVertex)];

    properties![(82, rotation, set_rotation), (83, distance, set_distance), cubic_vertex];
}

impl OnAdded for ObjectRef<'_, CubicMirroredVertex> {
    on_added!(CubicVertex);
}
