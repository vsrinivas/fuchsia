// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::rc::Rc;

use crate::{
    component::Component,
    component_dirt::ComponentDirt,
    core::{Core, Object, ObjectRef, OnAdded},
    shapes::{ParametricPath, Path, PathVertex, StraightVertex},
};

#[derive(Debug)]
pub struct Triangle {
    parametric_path: ParametricPath,
    vertex1: Rc<StraightVertex>,
    vertex2: Rc<StraightVertex>,
    vertex3: Rc<StraightVertex>,
}

impl ObjectRef<'_, Triangle> {
    pub fn update(&self, value: ComponentDirt) {
        if Component::value_has_dirt(value, ComponentDirt::PATH) {
            let parametric_path = self.cast::<ParametricPath>();

            let width = parametric_path.width();
            let height = parametric_path.height();

            let ox = -parametric_path.origin_x() * width;
            let oy = -parametric_path.origin_y() * height;

            let vertex1_ref = ObjectRef::from(&*self.vertex1);
            vertex1_ref.cast::<PathVertex>().set_x(ox + width * 0.5);
            vertex1_ref.cast::<PathVertex>().set_y(oy);

            let vertex2_ref = ObjectRef::from(&*self.vertex2);
            vertex2_ref.cast::<PathVertex>().set_x(ox + width);
            vertex2_ref.cast::<PathVertex>().set_y(oy + height);

            let vertex3_ref = ObjectRef::from(&*self.vertex3);
            vertex3_ref.cast::<PathVertex>().set_x(ox);
            vertex3_ref.cast::<PathVertex>().set_y(oy + height);
        }

        self.cast::<Path>().update(value);
    }
}

impl Core for Triangle {
    parent_types![(parametric_path, ParametricPath)];

    properties!(parametric_path);
}

impl OnAdded for ObjectRef<'_, Triangle> {
    on_added!(ParametricPath);
}

impl Default for Triangle {
    fn default() -> Self {
        let triangle = Self {
            parametric_path: ParametricPath::default(),
            vertex1: Rc::new(StraightVertex::default()),
            vertex2: Rc::new(StraightVertex::default()),
            vertex3: Rc::new(StraightVertex::default()),
        };

        let triangle_ref = ObjectRef::from(&triangle);
        let path = triangle_ref.cast::<Path>();

        path.push_vertex(Object::new(&(triangle.vertex1.clone() as Rc<dyn Core>)));
        path.push_vertex(Object::new(&(triangle.vertex2.clone() as Rc<dyn Core>)));
        path.push_vertex(Object::new(&(triangle.vertex3.clone() as Rc<dyn Core>)));

        triangle
    }
}
