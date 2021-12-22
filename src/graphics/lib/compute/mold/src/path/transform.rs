// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::iter;

use crate::{path::PathCommand, point::Point};

fn full_transform(point: &[f32; 3], transform: &[f32; 9]) -> [f32; 3] {
    [
        transform[0] * point[0] + transform[1] * point[1] + transform[2] * point[2],
        transform[3] * point[0] + transform[4] * point[1] + transform[5] * point[2],
        transform[6] * point[0] + transform[7] * point[1] + transform[8] * point[2],
    ]
}

fn simple_transform(point: Point<f32>, transform: &[f32; 9]) -> Point<f32> {
    Point::new(
        transform[0] * point.x + transform[1] * point.y + transform[2],
        transform[3] * point.x + transform[4] * point.y + transform[5],
    )
}

#[derive(Clone, Debug)]
pub(crate) struct Transform {
    transform: [f32; 9],
    is_perspective: bool,
}

macro_rules! simple {
    ( $slf:expr, $points:expr, $command:ident ) => {{
        let mut new_points = $points;

        for point in &mut new_points {
            *point = simple_transform(*point, &$slf.transform);
        }

        PathCommand::$command(new_points)
    }};
}

macro_rules! full {
    ( $slf:expr, $points:expr, $size:expr, $command:ident ) => {{
        let mut new_points = [(Point::default(), 0.0); $size];

        for ((point, weight), (new_point, new_weight)) in $points.zip(new_points.iter_mut()) {
            let transformed = full_transform(&[point.x, point.y, *weight], &$slf.transform);
            *new_point = Point::new(transformed[0], transformed[1]);
            *new_weight = transformed[2];
        }

        PathCommand::$command(new_points)
    }};
}

impl Transform {
    pub fn new(transform: &[f32; 9]) -> Self {
        let mut transform = *transform;
        let t22_recip = transform[8].recip();

        for t in &mut transform {
            *t *= t22_recip;
        }

        let is_perspective =
            transform[6].abs() > std::f32::EPSILON || transform[7].abs() > std::f32::EPSILON;

        Self { transform, is_perspective }
    }

    pub fn transform(&self, command: PathCommand) -> PathCommand {
        let ones = iter::repeat(&1.0);
        match (command, self.is_perspective) {
            (PathCommand::Line(points), false) => simple!(self, points, Line),
            (PathCommand::Line(points), true) => {
                let mut new_points = points;

                for point in &mut new_points {
                    let transformed = full_transform(&[point.x, point.y, 1.0], &self.transform);
                    *point = Point::from_weighted(&transformed);
                }

                PathCommand::Line(new_points)
            }
            (PathCommand::Quad(points), false) => simple!(self, points, Quad),
            (PathCommand::Quad(points), true) => full!(self, points.iter().zip(ones), 3, RatQuad),
            (PathCommand::RatQuad(points), _) => full!(self, points.iter(), 3, RatQuad),
            (PathCommand::Cubic(points), false) => simple!(self, points, Cubic),
            (PathCommand::Cubic(points), true) => full!(self, points.iter().zip(ones), 4, RatCubic),
            (PathCommand::RatCubic(points), _) => full!(self, points.iter(), 4, RatCubic),
            (PathCommand::Close, _) => PathCommand::Close,
        }
    }
}
