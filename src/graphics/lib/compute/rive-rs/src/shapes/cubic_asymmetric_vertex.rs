// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    core::{Core, ObjectRef, OnAdded, Property},
    math,
    shapes::{CubicVertex, PathVertex},
};

#[derive(Debug, Default)]
pub struct CubicAsymmetricVertex {
    cubic_vertex: CubicVertex,
    rotation: Property<f32>,
    in_distance: Property<f32>,
    out_distance: Property<f32>,
}

impl ObjectRef<'_, CubicAsymmetricVertex> {
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

    pub fn in_distance(&self) -> f32 {
        self.in_distance.get()
    }

    pub fn set_in_distance(&self, in_distance: f32) {
        if self.in_distance() == in_distance {
            return;
        }

        self.in_distance.set(in_distance);

        self.cast::<CubicVertex>().invalidate_in();
        self.cast::<PathVertex>().mark_path_dirty();
    }

    pub fn out_distance(&self) -> f32 {
        self.out_distance.get()
    }

    pub fn set_out_distance(&self, out_distance: f32) {
        if self.out_distance() == out_distance {
            return;
        }

        self.out_distance.set(out_distance);

        self.cast::<CubicVertex>().invalidate_out();
        self.cast::<PathVertex>().mark_path_dirty();
    }
}

impl ObjectRef<'_, CubicAsymmetricVertex> {
    pub fn compute_in(&self) {
        let pos = math::Vec::new(self.cast::<PathVertex>().x(), self.cast::<PathVertex>().y());
        let diff = math::Vec::new(
            self.rotation().cos() * -self.in_distance(),
            self.rotation().sin() * -self.in_distance(),
        );

        self.cast::<CubicVertex>().set_in_point(pos + diff);
    }

    pub fn compute_out(&self) {
        let pos = math::Vec::new(self.cast::<PathVertex>().x(), self.cast::<PathVertex>().y());
        let diff = math::Vec::new(
            self.rotation().cos() * self.out_distance(),
            self.rotation().sin() * self.out_distance(),
        );

        self.cast::<CubicVertex>().set_out_point(pos + diff);
    }
}

impl Core for CubicAsymmetricVertex {
    parent_types![(cubic_vertex, CubicVertex)];

    properties![
        (79, rotation, set_rotation),
        (80, in_distance, set_in_distance),
        (81, out_distance, set_out_distance),
        cubic_vertex,
    ];
}

impl OnAdded for ObjectRef<'_, CubicAsymmetricVertex> {
    on_added!(CubicVertex);
}
