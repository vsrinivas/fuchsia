// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::Cell;

use crate::{
    bones::{CubicWeight, Weight},
    core::{Core, ObjectRef, OnAdded},
    math::{self, Mat},
    shapes::{CubicAsymmetricVertex, CubicDetachedVertex, CubicMirroredVertex, PathVertex},
};

#[derive(Debug, Default)]
pub struct CubicVertex {
    path_vertex: PathVertex,
    in_is_valid: Cell<bool>,
    out_is_valid: Cell<bool>,
    in_point: Cell<math::Vec>,
    out_point: Cell<math::Vec>,
}

impl ObjectRef<'_, CubicVertex> {
    pub fn in_point(&self) -> math::Vec {
        if !self.in_is_valid.get() {
            self.compute_in();
            self.in_is_valid.set(true);
        }

        self.in_point.get()
    }

    pub fn set_in_point(&self, in_point: math::Vec) {
        self.in_point.set(in_point);
        self.in_is_valid.set(true);
    }

    pub fn out_point(&self) -> math::Vec {
        if !self.out_is_valid.get() {
            self.compute_out();
            self.out_is_valid.set(true);
        }

        self.out_point.get()
    }

    pub fn set_out_point(&self, out_point: math::Vec) {
        self.out_point.set(out_point);
        self.out_is_valid.set(true);
    }

    pub fn render_in(&self) -> math::Vec {
        if let Some(weight) = self.cast::<PathVertex>().weight() {
            weight.as_ref().cast::<CubicWeight>().in_translation()
        } else {
            self.in_point()
        }
    }

    pub fn render_out(&self) -> math::Vec {
        if let Some(weight) = self.cast::<PathVertex>().weight() {
            weight.as_ref().cast::<CubicWeight>().out_translation()
        } else {
            self.out_point()
        }
    }

    pub fn compute_in(&self) {
        if let Some(cubic_asymmetric_vertex) = self.try_cast::<CubicAsymmetricVertex>() {
            return cubic_asymmetric_vertex.compute_in();
        }

        if let Some(cubic_detached_vertex) = self.try_cast::<CubicDetachedVertex>() {
            return cubic_detached_vertex.compute_in();
        }

        if let Some(cubic_mirrored_vertex) = self.try_cast::<CubicMirroredVertex>() {
            return cubic_mirrored_vertex.compute_in();
        }

        unreachable!();
    }

    pub fn compute_out(&self) {
        if let Some(cubic_asymmetric_vertex) = self.try_cast::<CubicAsymmetricVertex>() {
            return cubic_asymmetric_vertex.compute_out();
        }

        if let Some(cubic_detached_vertex) = self.try_cast::<CubicDetachedVertex>() {
            return cubic_detached_vertex.compute_out();
        }

        if let Some(cubic_mirrored_vertex) = self.try_cast::<CubicMirroredVertex>() {
            return cubic_mirrored_vertex.compute_out();
        }

        unreachable!();
    }

    pub fn deform(&self, world_transform: Mat, bone_transforms: &[Cell<Mat>]) {
        let cubic_weight = self
            .cast::<PathVertex>()
            .weight()
            .expect("CubicVertex must have a CubicWeight")
            .cast::<CubicWeight>();
        let cubic_weight = cubic_weight.as_ref();

        let in_point = self.in_point();
        cubic_weight.set_in_translation(Weight::deform(
            in_point.x,
            in_point.y,
            cubic_weight.in_indices() as usize,
            cubic_weight.in_values() as usize,
            world_transform,
            bone_transforms,
        ));

        let out_point = self.out_point();
        cubic_weight.set_out_translation(Weight::deform(
            out_point.x,
            out_point.y,
            cubic_weight.out_indices() as usize,
            cubic_weight.out_values() as usize,
            world_transform,
            bone_transforms,
        ));
    }

    pub(crate) fn invalidate_in(&self) {
        self.in_is_valid.set(false);
    }

    pub(crate) fn invalidate_out(&self) {
        self.out_is_valid.set(false);
    }
}

impl Core for CubicVertex {
    parent_types![(path_vertex, PathVertex)];

    properties!(path_vertex);
}

impl OnAdded for ObjectRef<'_, CubicVertex> {
    on_added!(PathVertex);
}
