// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    render::generic::{forma::Forma, PathBuilder},
    Point,
};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FormaPath {
    pub(crate) path: forma::Path,
}

fn to_forma_point(point: Point) -> forma::Point {
    forma::Point::new(point.x, point.y)
}

#[derive(Debug)]
pub struct FormaPathBuilder {
    builder: forma::PathBuilder,
}

impl FormaPathBuilder {
    pub(crate) fn new() -> Self {
        Self { builder: forma::PathBuilder::new() }
    }
}

impl PathBuilder<Forma> for FormaPathBuilder {
    fn move_to(&mut self, point: Point) -> &mut Self {
        self.builder.move_to(to_forma_point(point));
        self
    }

    fn line_to(&mut self, point: Point) -> &mut Self {
        self.builder.line_to(to_forma_point(point));
        self
    }

    fn quad_to(&mut self, p1: Point, p2: Point) -> &mut Self {
        self.builder.quad_to(to_forma_point(p1), to_forma_point(p2));
        self
    }

    fn cubic_to(&mut self, p1: Point, p2: Point, p3: Point) -> &mut Self {
        self.builder.cubic_to(to_forma_point(p1), to_forma_point(p2), to_forma_point(p3));
        self
    }

    fn rat_quad_to(&mut self, p1: Point, p2: Point, w: f32) -> &mut Self {
        self.builder.rat_quad_to(to_forma_point(p1), to_forma_point(p2), w);
        self
    }

    fn rat_cubic_to(&mut self, p1: Point, p2: Point, p3: Point, w1: f32, w2: f32) -> &mut Self {
        self.builder.rat_cubic_to(
            to_forma_point(p1),
            to_forma_point(p2),
            to_forma_point(p3),
            w1,
            w2,
        );
        self
    }

    fn build(mut self) -> FormaPath {
        FormaPath { path: self.builder.build() }
    }
}
