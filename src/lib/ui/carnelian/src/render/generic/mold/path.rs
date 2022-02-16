// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    render::generic::{mold::Mold, PathBuilder},
    Point,
};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MoldPath {
    pub(crate) path: mold::Path,
}

fn to_mold_point(point: Point) -> mold::Point {
    mold::Point::new(point.x, point.y)
}

#[derive(Debug)]
pub struct MoldPathBuilder {
    builder: mold::PathBuilder,
}

impl MoldPathBuilder {
    pub(crate) fn new() -> Self {
        Self { builder: mold::PathBuilder::new() }
    }
}

impl PathBuilder<Mold> for MoldPathBuilder {
    fn move_to(&mut self, point: Point) -> &mut Self {
        self.builder.move_to(to_mold_point(point));
        self
    }

    fn line_to(&mut self, point: Point) -> &mut Self {
        self.builder.line_to(to_mold_point(point));
        self
    }

    fn quad_to(&mut self, p1: Point, p2: Point) -> &mut Self {
        self.builder.quad_to(to_mold_point(p1), to_mold_point(p2));
        self
    }

    fn cubic_to(&mut self, p1: Point, p2: Point, p3: Point) -> &mut Self {
        self.builder.cubic_to(to_mold_point(p1), to_mold_point(p2), to_mold_point(p3));
        self
    }

    fn rat_quad_to(&mut self, p1: Point, p2: Point, w: f32) -> &mut Self {
        self.builder.rat_quad_to(to_mold_point(p1), to_mold_point(p2), w);
        self
    }

    fn rat_cubic_to(&mut self, p1: Point, p2: Point, p3: Point, w1: f32, w2: f32) -> &mut Self {
        self.builder.rat_cubic_to(to_mold_point(p1), to_mold_point(p2), to_mold_point(p3), w1, w2);
        self
    }

    fn build(mut self) -> MoldPath {
        MoldPath { path: self.builder.build() }
    }
}
