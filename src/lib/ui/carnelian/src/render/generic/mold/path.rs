// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::rc::Rc;

use mold;

use crate::{
    render::generic::{mold::Mold, PathBuilder},
    Point,
};

#[derive(Clone, Debug)]
pub struct MoldPath {
    pub(crate) path: Rc<mold::Path>,
}

impl Eq for MoldPath {}

impl PartialEq for MoldPath {
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.path, &other.path)
    }
}

fn to_mold_point(point: Point) -> mold::Point<f32> {
    mold::Point::new(point.x, point.y)
}

#[derive(Debug)]
pub struct MoldPathBuilder {
    path: mold::Path,
    end_point: mold::Point<f32>,
}

impl MoldPathBuilder {
    pub(crate) fn new() -> Self {
        Self { path: mold::Path::new(), end_point: mold::Point::new(0.0, 0.0) }
    }
}

impl PathBuilder<Mold> for MoldPathBuilder {
    fn move_to(&mut self, point: Point) -> &mut Self {
        self.end_point = to_mold_point(point);
        self
    }

    fn line_to(&mut self, point: Point) -> &mut Self {
        let point = to_mold_point(point);
        self.path.line(self.end_point, point);
        self.end_point = point;
        self
    }

    fn quad_to(&mut self, p1: Point, p2: Point) -> &mut Self {
        let p1 = to_mold_point(p1);
        let p2 = to_mold_point(p2);
        self.path.quad(self.end_point, p1, p2);
        self.end_point = p2;
        self
    }

    fn cubic_to(&mut self, p1: Point, p2: Point, p3: Point) -> &mut Self {
        let p1 = to_mold_point(p1);
        let p2 = to_mold_point(p2);
        let p3 = to_mold_point(p3);
        self.path.cubic(self.end_point, p1, p2, p3);
        self.end_point = p3;
        self
    }

    fn build(self) -> MoldPath {
        MoldPath { path: Rc::new(self.path) }
    }
}
