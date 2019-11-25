// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    path::{transform::Transform, PathCommand},
    point::Point,
    segment::Segment,
    PIXEL_WIDTH,
};

// Pixel accuracy should be within 0.5 of a sub-pixel.
const PIXEL_ACCURACY: f32 = 0.5 / PIXEL_WIDTH as f32;

fn lerp(t: f32, a: f32, b: f32) -> f32 {
    a * (1.0 - t) + b * t
}

macro_rules! lerp {
    ( $t:expr, $v0:expr, $v1:expr $( , )? ) => {
        lerp($t, $v0, $v1)
    };
    ( $t:expr, $v0:expr, $v1:expr, $v2:expr $( , )? ) => {
        lerp($t, lerp!($t, $v0, $v1), lerp!($t, $v1, $v2))
    };
    ( $t:expr, $v0:expr, $v1:expr, $v2:expr, $v3:expr $( , )? ) => {
        lerp($t, lerp!($t, $v0, $v1, $v2), lerp!($t, $v1, $v2, $v3))
    };
}

#[derive(Clone, Debug)]
struct CurveState {
    pub subdivisions: usize,
    pub t: f32,
    pub increment: f32,
    pub point: Point<f32>,
}

#[derive(Clone, Debug)]
pub(crate) struct PathSegments<'p> {
    commands: &'p [PathCommand],
    transform: Option<Transform>,
    state: Option<CurveState>,
    last_point: Option<Point<f32>>,
    closing_point: Option<Point<f32>>,
}

/// Computes the number of subdivisions that are need to subdivide a a curve in order to achieve the
/// right accuracy. It uses Wang's Formula, which guarantees this accuracy given some deviation
/// parameters.
fn subdivisions(
    segments: &mut PathSegments<'_>,
    first: Point<f32>,
    last: Point<f32>,
    deviation_x: f32,
    deviation_y: f32,
) -> Result<usize, Segment<f32>> {
    let deviation_squared = deviation_x * deviation_x + deviation_y * deviation_y;
    if deviation_squared < PIXEL_ACCURACY {
        return Err(segments.line(&[first, last]));
    }

    let segments_count =
        (deviation_squared.sqrt() * 2.0 / (8.0 * PIXEL_ACCURACY)).sqrt().ceil() as usize;
    let subdivisions = segments_count.max(1) - 1;

    if subdivisions == 0 {
        return Err(segments.line(&[first, last]));
    }

    Ok(subdivisions)
}

macro_rules! bezier {
    ( $t:expr, $p0:expr, $p1:expr $( , )? ) => {
        Point::lerp($t, $p0, $p1)
    };
    ( $t:expr, $p0:expr, $p1:expr, $p2:expr $( , )? ) => {
        Point::lerp($t, bezier!($t, $p0, $p1), bezier!($t, $p1, $p2))
    };
    ( $t:expr, $p0:expr, $p1:expr, $p2:expr, $p3:expr $( , )? ) => {
        Point::lerp($t, bezier!($t, $p0, $p1, $p2), bezier!($t, $p1, $p2, $p3))
    };
}

macro_rules! deviation {
    ( $field:ident, $p0:expr, $p1:expr, $p2:expr $( , )? ) => {{
        $p0.$field + $p2.$field - 2.0 * $p1.$field
    }};
    ( $field:ident, $p0:expr, $p1:expr, $p2:expr, $p3:expr $( , )? ) => {{
        ($p0.$field + $p2.$field - 2.0 * $p1.$field)
            .abs()
            .max(($p1.$field + $p3.$field - 2.0 * $p2.$field).abs())
    }};
}

impl<'p> PathSegments<'p> {
    pub fn new(commands: &'p [PathCommand], transform: Option<Transform>) -> Self {
        PathSegments { commands, transform, state: None, last_point: None, closing_point: None }
    }

    fn from_weighted(point: (Point<f32>, f32)) -> Point<f32> {
        Point::from_weighted(&[point.0.x, point.0.y, point.1])
    }

    fn line(&mut self, points: &[Point<f32>; 2]) -> Segment<f32> {
        if self.closing_point.is_none() {
            self.closing_point = Some(points[0]);
        }

        self.last_point = Some(points[1]);

        Segment::new(points[0], points[1])
    }

    fn next_segment(
        &mut self,
        first: Point<f32>,
        last: Point<f32>,
        deviation_x: f32,
        deviation_y: f32,
        next: impl Fn(f32) -> Point<f32>,
    ) -> Segment<f32> {
        match self.state.take() {
            Some(mut state) => {
                if state.subdivisions == 1 {
                    self.state = None;

                    self.line(&[state.point, last])
                } else {
                    state.t += state.increment;

                    let next_point = next(state.t);

                    let segment = self.line(&[state.point, next_point]);

                    state.point = next_point;
                    state.subdivisions -= 1;

                    self.state = Some(state);

                    segment
                }
            }
            None => {
                let subdivisions = match subdivisions(self, first, last, deviation_x, deviation_y) {
                    Ok(subdivisions) => subdivisions,
                    Err(segment) => return segment,
                };
                let increment = (subdivisions as f32).recip();

                self.state = Some(CurveState { subdivisions, increment, t: 0.0, point: first });

                self.next_segment(first, last, deviation_x, deviation_y, next)
            }
        }
    }

    fn quad(&mut self, points: &[Point<f32>; 3]) -> Segment<f32> {
        self.next_segment(
            points[0],
            points[2],
            deviation!(x, points[0], points[1], points[2]),
            deviation!(y, points[0], points[1], points[2]),
            |t| bezier!(t, points[0], points[1], points[2]),
        )
    }

    fn rat_quad(&mut self, points: &[(Point<f32>, f32); 3]) -> Segment<f32> {
        let p0 = Self::from_weighted(points[0]);
        let p1 = Self::from_weighted(points[1]);
        let p2 = Self::from_weighted(points[2]);

        self.next_segment(p0, p2, deviation!(x, p0, p1, p2), deviation!(y, p0, p1, p2), |t| {
            bezier!(
                t,
                points[0].0 * points[0].1,
                points[1].0 * points[1].1,
                points[2].0 * points[2].1,
            ) * lerp!(t, points[0].1, points[1].1, points[2].1).recip()
        })
    }

    fn cubic(&mut self, points: &[Point<f32>; 4]) -> Segment<f32> {
        self.next_segment(
            points[0],
            points[3],
            deviation!(x, points[0], points[1], points[2], points[3]),
            deviation!(y, points[0], points[1], points[2], points[3]),
            |t| bezier!(t, points[0], points[1], points[2], points[3]),
        )
    }

    fn rat_cubic(&mut self, points: &[(Point<f32>, f32); 4]) -> Segment<f32> {
        let p0 = Self::from_weighted(points[0]);
        let p1 = Self::from_weighted(points[1]);
        let p2 = Self::from_weighted(points[2]);
        let p3 = Self::from_weighted(points[3]);

        self.next_segment(
            p0,
            p3,
            deviation!(x, p0, p1, p2, p3),
            deviation!(y, p0, p1, p2, p3),
            |t| {
                bezier!(
                    t,
                    points[0].0 * points[0].1,
                    points[1].0 * points[1].1,
                    points[2].0 * points[2].1,
                    points[3].0 * points[3].1,
                ) * lerp!(t, points[0].1, points[1].1, points[2].1, points[3].1).recip()
            },
        )
    }

    fn close(&mut self) -> Option<Segment<f32>> {
        let last_point = self.last_point;
        if let (Some(closing_point), Some(last_point)) = (self.closing_point, last_point) {
            let segment = if !closing_point.approx_eq(last_point) {
                Some(self.line(&[last_point, closing_point]))
            } else {
                None
            };

            self.closing_point = None;

            return segment;
        }

        None
    }

    fn first(&self) -> Option<PathCommand> {
        self.commands.first().map(|&command| match &self.transform {
            Some(transform) => transform.transform(command),
            None => command,
        })
    }
}

impl Iterator for PathSegments<'_> {
    type Item = Segment<f32>;

    fn next(&mut self) -> Option<Self::Item> {
        if let Some(PathCommand::Close) = self.first() {
            self.commands = &self.commands[1..];

            return match self.close() {
                Some(segment) => Some(segment),
                None => self.next(),
            };
        }

        self.first().map(|command| {
            let segment = match command {
                PathCommand::Line(points) => self.line(&points),
                PathCommand::Quad(points) => self.quad(&points),
                PathCommand::RatQuad(points) => self.rat_quad(&points),
                PathCommand::Cubic(points) => self.cubic(&points),
                PathCommand::RatCubic(points) => self.rat_cubic(&points),
                _ => unreachable!(),
            };

            if self.state.is_none() {
                self.commands = &self.commands[1..];
            }

            segment
        })
    }
}
