// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::Cell;

use crate::{
    bones::Weight,
    container_component::ContainerComponent,
    core::{Core, CoreContext, Object, ObjectRef, OnAdded, Property},
    math::{self, Mat},
    option_cell::OptionCell,
    shapes::Path,
    status_code::StatusCode,
    Component,
};

use super::CubicVertex;

#[derive(Debug, Default)]
pub struct PathVertex {
    container_component: ContainerComponent,
    x: Property<f32>,
    y: Property<f32>,
    weight: OptionCell<Object<Weight>>,
}

impl ObjectRef<'_, PathVertex> {
    pub fn x(&self) -> f32 {
        self.x.get()
    }

    pub fn set_x(&self, x: f32) {
        if self.x() == x {
            return;
        }

        self.x.set(x);

        self.mark_path_dirty();

        if let Some(cubic_vertex) = self.try_cast::<CubicVertex>() {
            cubic_vertex.invalidate_in();
            cubic_vertex.invalidate_out();
        }
    }

    pub fn y(&self) -> f32 {
        self.y.get()
    }

    pub fn set_y(&self, y: f32) {
        if self.y() == y {
            return;
        }

        self.y.set(y);

        self.mark_path_dirty();

        if let Some(cubic_vertex) = self.try_cast::<CubicVertex>() {
            cubic_vertex.invalidate_in();
            cubic_vertex.invalidate_out();
        }
    }
}

impl ObjectRef<'_, PathVertex> {
    fn parent_path(&self) -> Option<Object<Path>> {
        self.cast::<Component>().parent().and_then(|parent| parent.try_cast::<Path>())
    }

    pub(crate) fn mark_path_dirty(&self) {
        if let Some(path) = self.parent_path() {
            path.as_ref().mark_path_dirty();
        }
    }

    pub fn weight(&self) -> Option<Object<Weight>> {
        self.weight.get()
    }

    pub(crate) fn set_weight(&self, weight: Object<Weight>) {
        self.weight.set(Some(weight));
    }

    pub fn deform(&self, world_transform: Mat, bone_transforms: &[Cell<Mat>]) {
        if let Some(weight) = self.weight() {
            let weight = weight.as_ref();
            weight.set_translation(Weight::deform(
                self.x(),
                self.y(),
                weight.indices() as usize,
                weight.values() as usize,
                world_transform,
                bone_transforms,
            ));
        }

        if let Some(cubic_vertex) = self.try_cast::<CubicVertex>() {
            cubic_vertex.deform(world_transform, bone_transforms);
        }
    }

    pub fn render_translation(&self) -> math::Vec {
        self.weight()
            .map(|weight| weight.as_ref().translation())
            .unwrap_or_else(|| math::Vec::new(self.x(), self.y()))
    }
}

impl Core for PathVertex {
    parent_types![(container_component, ContainerComponent)];

    properties![(24, x, set_x), (25, y, set_y), container_component];
}

impl OnAdded for ObjectRef<'_, PathVertex> {
    on_added!([on_added_clean, import], ContainerComponent);

    fn on_added_dirty(&self, context: &dyn CoreContext) -> StatusCode {
        let code = self.cast::<ContainerComponent>().on_added_dirty(context);
        if code != StatusCode::Ok {
            return code;
        }

        if let Some(path) = self.parent_path() {
            path.as_ref().push_vertex(self.as_object());
            StatusCode::Ok
        } else {
            StatusCode::MissingObject
        }
    }
}
