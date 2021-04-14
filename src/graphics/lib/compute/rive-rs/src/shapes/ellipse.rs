// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::rc::Rc;

use crate::{
    component::Component,
    component_dirt::ComponentDirt,
    core::{Core, Object, ObjectRef, OnAdded},
    math,
    shapes::{CubicDetachedVertex, CubicVertex, ParametricPath, Path, PathVertex},
};

#[derive(Debug)]
pub struct Ellipse {
    parametric_path: ParametricPath,
    vertex1: Rc<CubicDetachedVertex>,
    vertex2: Rc<CubicDetachedVertex>,
    vertex3: Rc<CubicDetachedVertex>,
    vertex4: Rc<CubicDetachedVertex>,
}

impl ObjectRef<'_, Ellipse> {
    pub fn update(&self, value: ComponentDirt) {
        let parametric_path = self.cast::<ParametricPath>();

        if Component::value_has_dirt(value, ComponentDirt::PATH) {
            let radius_x = parametric_path.width() / 2.0;
            let radius_y = parametric_path.height() / 2.0;

            let o_x = -parametric_path.origin_x() * parametric_path.width() + radius_x;
            let o_y = -parametric_path.origin_y() * parametric_path.height() + radius_y;

            fn vertex_ref(vertex: &Rc<CubicDetachedVertex>) -> ObjectRef<'_, CubicDetachedVertex> {
                ObjectRef::from(&**vertex)
            }

            let v1 = vertex_ref(&self.vertex1);
            let v2 = vertex_ref(&self.vertex2);
            let v3 = vertex_ref(&self.vertex3);
            let v4 = vertex_ref(&self.vertex4);

            v1.cast::<PathVertex>().set_x(o_x);
            v1.cast::<PathVertex>().set_y(o_y - radius_y);
            v1.cast::<CubicVertex>().set_in_point(math::Vec::new(
                o_x - radius_x * math::CIRCLE_CONSTANT,
                o_y - radius_y,
            ));
            v1.cast::<CubicVertex>().set_out_point(math::Vec::new(
                o_x + radius_x * math::CIRCLE_CONSTANT,
                o_y - radius_y,
            ));

            v2.cast::<PathVertex>().set_x(o_x + radius_x);
            v2.cast::<PathVertex>().set_y(o_y);
            v2.cast::<CubicVertex>().set_in_point(math::Vec::new(
                o_x + radius_x,
                o_y - radius_y * math::CIRCLE_CONSTANT,
            ));
            v2.cast::<CubicVertex>().set_out_point(math::Vec::new(
                o_x + radius_x,
                o_y + radius_y * math::CIRCLE_CONSTANT,
            ));

            v3.cast::<PathVertex>().set_x(o_x);
            v3.cast::<PathVertex>().set_y(o_y + radius_y);
            v3.cast::<CubicVertex>().set_in_point(math::Vec::new(
                o_x + radius_x * math::CIRCLE_CONSTANT,
                o_y + radius_y,
            ));
            v3.cast::<CubicVertex>().set_out_point(math::Vec::new(
                o_x - radius_x * math::CIRCLE_CONSTANT,
                o_y + radius_y,
            ));

            v4.cast::<PathVertex>().set_x(o_x - radius_x);
            v4.cast::<PathVertex>().set_y(o_y);
            v4.cast::<CubicVertex>().set_in_point(math::Vec::new(
                o_x - radius_x,
                o_y + radius_y * math::CIRCLE_CONSTANT,
            ));
            v4.cast::<CubicVertex>().set_out_point(math::Vec::new(
                o_x - radius_x,
                o_y - radius_y * math::CIRCLE_CONSTANT,
            ));
        }

        self.cast::<Path>().update(value);
    }
}

impl Core for Ellipse {
    parent_types![(parametric_path, ParametricPath)];

    properties!(parametric_path);
}

impl OnAdded for ObjectRef<'_, Ellipse> {
    on_added!(ParametricPath);
}

impl Default for Ellipse {
    fn default() -> Self {
        let ellipse = Self {
            parametric_path: ParametricPath::default(),
            vertex1: Rc::new(CubicDetachedVertex::default()),
            vertex2: Rc::new(CubicDetachedVertex::default()),
            vertex3: Rc::new(CubicDetachedVertex::default()),
            vertex4: Rc::new(CubicDetachedVertex::default()),
        };

        let ellipse_ref = ObjectRef::from(&ellipse);
        let path = ellipse_ref.cast::<Path>();

        let v1 = ellipse.vertex1.clone() as Rc<dyn Core>;
        let v2 = ellipse.vertex2.clone() as Rc<dyn Core>;
        let v3 = ellipse.vertex3.clone() as Rc<dyn Core>;
        let v4 = ellipse.vertex4.clone() as Rc<dyn Core>;

        path.push_vertex(Object::new(&v1));
        path.push_vertex(Object::new(&v2));
        path.push_vertex(Object::new(&v3));
        path.push_vertex(Object::new(&v4));

        ellipse
    }
}
