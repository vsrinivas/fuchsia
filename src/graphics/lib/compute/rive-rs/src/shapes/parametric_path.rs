// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    core::{Core, ObjectRef, OnAdded, Property},
    shapes::path::Path,
};

#[derive(Debug)]
pub struct ParametricPath {
    path: Path,
    width: Property<f32>,
    height: Property<f32>,
    origin_x: Property<f32>,
    origin_y: Property<f32>,
}

impl ObjectRef<'_, ParametricPath> {
    pub fn width(&self) -> f32 {
        self.width.get()
    }

    pub fn set_width(&self, width: f32) {
        if self.width() == width {
            return;
        }

        self.width.set(width);
        self.cast::<Path>().mark_path_dirty();
    }

    pub fn height(&self) -> f32 {
        self.height.get()
    }

    pub fn set_height(&self, height: f32) {
        if self.height() == height {
            return;
        }

        self.height.set(height);
        self.cast::<Path>().mark_path_dirty();
    }

    pub fn origin_x(&self) -> f32 {
        self.origin_x.get()
    }

    pub fn set_origin_x(&self, origin_x: f32) {
        self.origin_x.set(origin_x);
    }

    pub fn origin_y(&self) -> f32 {
        self.origin_y.get()
    }

    pub fn set_origin_y(&self, origin_y: f32) {
        self.origin_y.set(origin_y);
    }
}

impl Core for ParametricPath {
    parent_types![(path, Path)];

    properties![
        (20, width, set_width),
        (21, height, set_height),
        (123, origin_x, set_origin_x),
        (124, origin_y, set_origin_y),
        path,
    ];
}

impl OnAdded for ObjectRef<'_, ParametricPath> {
    on_added!(Path);
}

impl Default for ParametricPath {
    fn default() -> Self {
        Self {
            path: Path::default(),
            width: Property::new(0.0),
            height: Property::new(0.0),
            origin_x: Property::new(0.5),
            origin_y: Property::new(0.5),
        }
    }
}
