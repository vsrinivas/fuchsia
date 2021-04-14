// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::rc::Rc;

use crate::{
    component::Component,
    component_dirt::ComponentDirt,
    core::{Core, Object, ObjectRef, OnAdded, Property},
    shapes::{ParametricPath, Path, StraightVertex},
};

use super::PathVertex;

#[derive(Debug)]
pub struct Rectangle {
    parametric_path: ParametricPath,
    corner_radius: Property<f32>,
    vertex1: Rc<StraightVertex>,
    vertex2: Rc<StraightVertex>,
    vertex3: Rc<StraightVertex>,
    vertex4: Rc<StraightVertex>,
}

impl ObjectRef<'_, Rectangle> {
    pub fn corner_radius(&self) -> f32 {
        self.corner_radius.get()
    }

    pub fn set_corner_radius(&self, corner_radius: f32) {
        self.corner_radius.set(corner_radius);
        self.cast::<Path>().mark_path_dirty();
    }
}

impl ObjectRef<'_, Rectangle> {
    pub fn update(&self, value: ComponentDirt) {
        if Component::value_has_dirt(value, ComponentDirt::PATH) {
            let radius = self.corner_radius();
            let parametric_path = self.cast::<ParametricPath>();

            let width = parametric_path.width();
            let height = parametric_path.height();

            let ox = -parametric_path.origin_x() * width;
            let oy = -parametric_path.origin_y() * height;

            let vertex1_ref = ObjectRef::from(&*self.vertex1);
            vertex1_ref.cast::<PathVertex>().set_x(ox);
            vertex1_ref.cast::<PathVertex>().set_y(oy);
            vertex1_ref.set_radius(radius);

            let vertex2_ref = ObjectRef::from(&*self.vertex2);
            vertex2_ref.cast::<PathVertex>().set_x(ox + width);
            vertex2_ref.cast::<PathVertex>().set_y(oy);
            vertex2_ref.set_radius(radius);

            let vertex3_ref = ObjectRef::from(&*self.vertex3);
            vertex3_ref.cast::<PathVertex>().set_x(ox + width);
            vertex3_ref.cast::<PathVertex>().set_y(oy + height);
            vertex3_ref.set_radius(radius);

            let vertex4_ref = ObjectRef::from(&*self.vertex4);
            vertex4_ref.cast::<PathVertex>().set_x(ox);
            vertex4_ref.cast::<PathVertex>().set_y(oy + height);
            vertex4_ref.set_radius(radius);
        }

        self.cast::<Path>().update(value);
    }
}

impl Core for Rectangle {
    parent_types![(parametric_path, ParametricPath)];

    properties![(31, corner_radius, set_corner_radius), parametric_path];
}

impl OnAdded for ObjectRef<'_, Rectangle> {
    on_added!(ParametricPath);
}

impl Default for Rectangle {
    fn default() -> Self {
        let rectangle = Self {
            parametric_path: ParametricPath::default(),
            corner_radius: Property::new(0.0),
            vertex1: Rc::new(StraightVertex::default()),
            vertex2: Rc::new(StraightVertex::default()),
            vertex3: Rc::new(StraightVertex::default()),
            vertex4: Rc::new(StraightVertex::default()),
        };

        let rectangle_ref = ObjectRef::from(&rectangle);
        let path = rectangle_ref.cast::<Path>();

        path.push_vertex(Object::new(&(rectangle.vertex1.clone() as Rc<dyn Core>)));
        path.push_vertex(Object::new(&(rectangle.vertex2.clone() as Rc<dyn Core>)));
        path.push_vertex(Object::new(&(rectangle.vertex3.clone() as Rc<dyn Core>)));
        path.push_vertex(Object::new(&(rectangle.vertex4.clone() as Rc<dyn Core>)));

        rectangle
    }
}
