// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cmp::Ordering, rc::Rc};

use crate::{
    component::Component,
    component_dirt::ComponentDirt,
    core::{Core, Object, ObjectRef, OnAdded, Property},
    dyn_vec::DynVec,
    shapes::{ParametricPath, Path, PathVertex, StraightVertex},
};

#[derive(Debug, Default)]
pub struct Polygon {
    parametric_path: ParametricPath,
    points: Property<u64>,
    corner_radius: Property<f32>,
    vertices: DynVec<Rc<StraightVertex>>,
}

impl ObjectRef<'_, Polygon> {
    pub fn points(&self) -> u64 {
        self.points.get()
    }

    pub fn set_points(&self, points: u64) {
        self.points.set(points);
        self.cast::<Path>().mark_path_dirty();
    }

    pub fn corner_radius(&self) -> f32 {
        self.corner_radius.get()
    }

    pub fn set_corner_radius(&self, corner_radius: f32) {
        self.corner_radius.set(corner_radius);
        self.cast::<Path>().mark_path_dirty();
    }
}

impl ObjectRef<'_, Polygon> {
    fn expected_len(&self) -> usize {
        self.points() as usize
    }

    fn resize_vertices(&self, new_len: usize) {
        match self.vertices.len().cmp(&new_len) {
            Ordering::Less => {
                for _ in 0..new_len - self.vertices.len() {
                    let vertex = Rc::new(StraightVertex::default());

                    self.cast::<Path>().push_vertex(Object::new(&(vertex.clone() as Rc<dyn Core>)));
                    self.vertices.push(vertex);
                }
            }
            Ordering::Greater => {
                self.cast::<Path>().vertices.truncate(new_len);
                self.vertices.truncate(new_len);
            }
            _ => (),
        }
    }

    fn build_polygon(&self) {
        let parametric_path = self.cast::<ParametricPath>();

        let half_width = parametric_path.width() * 0.5;
        let half_height = parametric_path.height() * 0.5;

        let mut angle = -std::f32::consts::FRAC_PI_2;
        let increment = std::f32::consts::PI / self.points() as f32;

        for vertex in self.vertices.iter() {
            let (sin, cos) = angle.sin_cos();

            let vertex_ref = ObjectRef::from(&*vertex);
            vertex_ref.cast::<PathVertex>().set_x(cos * half_width);
            vertex_ref.cast::<PathVertex>().set_y(sin * half_height);
            vertex_ref.set_radius(self.corner_radius());

            angle += increment;
        }
    }

    pub fn update(&self, value: ComponentDirt) {
        let path = self.cast::<Path>();

        if Component::value_has_dirt(value, ComponentDirt::PATH) {
            let expected_len = self.expected_len();
            if self.vertices.len() != expected_len {
                self.resize_vertices(expected_len);
            }

            self.build_polygon();
        }

        path.update(value);
    }
}

impl Core for Polygon {
    parent_types![(parametric_path, ParametricPath)];

    properties!(
        (125, points, set_points),
        (126, corner_radius, set_corner_radius),
        parametric_path
    );
}

impl OnAdded for ObjectRef<'_, Polygon> {
    on_added!(ParametricPath);
}
