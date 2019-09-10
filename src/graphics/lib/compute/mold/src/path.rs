// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::f32;

use crate::{edge::Edge, point::Point};

const PIXEL_ACCURACY: f32 = 0.25;

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
struct Transform {
    transform: [f32; 9],
    is_perspective: bool,
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
        match (command, self.is_perspective) {
            (PathCommand::Line(points), _) => {
                let mut new_points = points;

                for point in &mut new_points {
                    let transformed = full_transform(&[point.x, point.y, 1.0], &self.transform);
                    *point = Point::from_weighted(&transformed);
                }

                PathCommand::Line(new_points)
            }
            (PathCommand::Quad(points), false) => {
                let mut new_points = points;

                for point in &mut new_points {
                    *point = simple_transform(*point, &self.transform);
                }

                PathCommand::Quad(new_points)
            }
            (PathCommand::Quad(points), true) => {
                let mut new_points = [(Point::default(), 0.0); 3];

                for (point, (new_point, weight)) in points.iter().zip(new_points.iter_mut()) {
                    let transformed = full_transform(&[point.x, point.y, 1.0], &self.transform);
                    *new_point = Point::new(transformed[0], transformed[1]);
                    *weight = transformed[2];
                }

                PathCommand::RatQuad(new_points)
            }
            (PathCommand::RatQuad(points), _) => {
                let mut new_points = [(Point::default(), 0.0); 3];

                for ((point, weight), (new_point, new_weight)) in
                    points.iter().zip(new_points.iter_mut())
                {
                    let transformed = full_transform(&[point.x, point.y, *weight], &self.transform);
                    *new_point = Point::new(transformed[0], transformed[1]);
                    *new_weight = transformed[2];
                }

                PathCommand::RatQuad(new_points)
            }
            (PathCommand::Cubic(points), false) => {
                let mut new_points = points;

                for point in &mut new_points {
                    *point = simple_transform(*point, &self.transform);
                }

                PathCommand::Cubic(new_points)
            }
            (PathCommand::Cubic(points), true) => {
                let mut new_points = [(Point::default(), 0.0); 4];

                for (point, (new_point, weight)) in points.iter().zip(new_points.iter_mut()) {
                    let transformed = full_transform(&[point.x, point.y, 1.0], &self.transform);
                    *new_point = Point::new(transformed[0], transformed[1]);
                    *weight = transformed[2];
                }

                PathCommand::RatCubic(new_points)
            }
            (PathCommand::RatCubic(points), _) => {
                let mut new_points = [(Point::default(), 0.0); 4];

                for ((point, weight), (new_point, new_weight)) in
                    points.iter().zip(new_points.iter_mut())
                {
                    let transformed = full_transform(&[point.x, point.y, *weight], &self.transform);
                    *new_point = Point::new(transformed[0], transformed[1]);
                    *new_weight = transformed[2];
                }

                PathCommand::RatCubic(new_points)
            }
            (PathCommand::Close, _) => PathCommand::Close,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
enum PathCommand {
    Line([Point<f32>; 2]),
    Quad([Point<f32>; 3]),
    RatQuad([(Point<f32>, f32); 3]),
    Cubic([Point<f32>; 4]),
    RatCubic([(Point<f32>, f32); 4]),
    Close,
}

#[derive(Clone, Debug, Default)]
pub struct Path {
    commands: Vec<PathCommand>,
}

impl Path {
    pub fn new() -> Self {
        Self { commands: vec![] }
    }

    pub fn line(&mut self, p0: Point<f32>, p1: Point<f32>) {
        self.commands.push(PathCommand::Line([p0, p1]));
    }

    pub fn quad(&mut self, p0: Point<f32>, p1: Point<f32>, p2: Point<f32>) {
        self.commands.push(PathCommand::Quad([p0, p1, p2]));
    }

    pub fn cubic(&mut self, p0: Point<f32>, p1: Point<f32>, p2: Point<f32>, p3: Point<f32>) {
        self.commands.push(PathCommand::Cubic([p0, p1, p2, p3]));
    }

    pub fn close(&mut self) {
        self.commands.push(PathCommand::Close);
    }

    pub fn edges(&self) -> Edges {
        Edges {
            commands: &self.commands,
            transform: None,
            state: None,
            last_point: None,
            closing_point: None,
        }
    }

    pub(crate) fn transformed(&self, transform: &[f32; 9]) -> Edges {
        Edges {
            commands: &self.commands,
            transform: Some(Transform::new(transform)),
            state: None,
            last_point: None,
            closing_point: None,
        }
    }
}

fn lerp(t: f32, a: f32, b: f32) -> f32 {
    a * (1.0 - t) + b * t
}

fn max(a: f32, b: f32) -> f32 {
    if a > b {
        a
    } else {
        b
    }
}

#[derive(Clone, Debug)]
struct CurveState {
    pub subdivisions: usize,
    pub t: f32,
    pub increment: f32,
    pub point: Point<f32>,
}

#[derive(Clone, Debug)]
pub struct Edges<'p> {
    commands: &'p [PathCommand],
    transform: Option<Transform>,
    state: Option<CurveState>,
    last_point: Option<Point<f32>>,
    closing_point: Option<Point<f32>>,
}

impl Edges<'_> {
    fn from_weighted(point: (Point<f32>, f32)) -> Point<f32> {
        Point::from_weighted(&[point.0.x, point.0.y, point.1])
    }

    fn line(&mut self, points: &[Point<f32>; 2]) -> Edge<f32> {
        if self.closing_point.is_none() {
            self.closing_point = Some(points[0]);
        }

        self.last_point = Some(points[1]);

        Edge::new(points[0], points[1])
    }

    fn increment(
        &mut self,
        first: Point<f32>,
        last: Point<f32>,
        init: impl Fn(&mut Self) -> Result<usize, Edge<f32>>,
        next: impl Fn(f32) -> Point<f32>,
    ) -> Edge<f32> {
        match self.state.take() {
            Some(mut state) => {
                if state.subdivisions == 1 {
                    self.state = None;

                    self.line(&[state.point, last])
                } else {
                    state.t += state.increment;

                    let next_point = next(state.t);

                    let edge = self.line(&[state.point, next_point]);

                    state.point = next_point;
                    state.subdivisions -= 1;

                    self.state = Some(state);

                    edge
                }
            }
            None => {
                let subdivisions = match init(self) {
                    Ok(subdivisions) => subdivisions,
                    Err(edge) => return edge,
                };
                let increment = (subdivisions as f32).recip();

                self.state = Some(CurveState { subdivisions, increment, t: 0.0, point: first });

                self.increment(first, last, init, next)
            }
        }
    }

    fn quad(&mut self, points: &[Point<f32>; 3]) -> Edge<f32> {
        self.increment(
            points[0],
            points[2],
            |edges| {
                let deviation_x = points[0].x + points[2].x - 2.0 * points[1].x;
                let deviation_y = points[0].y + points[2].y - 2.0 * points[1].y;
                let deviation_squared = deviation_x * deviation_x + deviation_y * deviation_y;

                if deviation_squared < PIXEL_ACCURACY {
                    return Err(edges.line(&[points[0], points[2]]));
                }

                let segments = (deviation_squared.sqrt() * 2.0 / (8.0 * PIXEL_ACCURACY))
                    .sqrt()
                    .ceil() as usize;
                let subdivisions = segments.max(1) - 1;

                if subdivisions == 0 {
                    return Err(edges.line(&[points[0], points[2]]));
                }

                Ok(subdivisions)
            },
            |t| {
                Point::lerp(
                    t,
                    Point::lerp(t, points[0], points[1]),
                    Point::lerp(t, points[1], points[2]),
                )
            },
        )
    }

    fn rat_quad(&mut self, points: &[(Point<f32>, f32); 3]) -> Edge<f32> {
        let p0 = Self::from_weighted(points[0]);
        let p1 = Self::from_weighted(points[1]);
        let p2 = Self::from_weighted(points[2]);

        self.increment(
            p0,
            p2,
            |edges| {
                let deviation_x = p0.x + p2.x - 2.0 * p1.x;
                let deviation_y = p0.y + p2.y - 2.0 * p1.y;
                let deviation_squared = deviation_x * deviation_x + deviation_y * deviation_y;

                if deviation_squared < PIXEL_ACCURACY {
                    return Err(edges.line(&[p0, p2]));
                }

                let segments = (deviation_squared.sqrt() * 2.0 / (8.0 * PIXEL_ACCURACY))
                    .sqrt()
                    .ceil() as usize;
                let subdivisions = segments.max(1) - 1;

                if subdivisions == 0 {
                    return Err(edges.line(&[p0, p2]));
                }

                Ok(subdivisions)
            },
            |t| {
                Point::lerp(
                    t,
                    Point::lerp(t, points[0].0 * points[0].1, points[1].0 * points[1].1),
                    Point::lerp(t, points[1].0 * points[1].1, points[2].0 * points[2].1),
                ) * lerp(t, lerp(t, points[0].1, points[1].1), lerp(t, points[1].1, points[2].1))
                    .recip()
            },
        )
    }

    fn cubic(&mut self, points: &[Point<f32>; 4]) -> Edge<f32> {
        self.increment(
            points[0],
            points[3],
            |edges| {
                let deviation_x = max(
                    (points[0].x + points[2].x - 2.0 * points[1].x).abs(),
                    (points[1].x + points[3].x - 2.0 * points[2].x).abs(),
                );
                let deviation_y = max(
                    (points[0].y + points[2].y - 2.0 * points[1].y).abs(),
                    (points[1].y + points[3].y - 2.0 * points[2].y).abs(),
                );
                let deviation_squared = deviation_x * deviation_x + deviation_y * deviation_y;

                if deviation_squared < PIXEL_ACCURACY {
                    return Err(edges.line(&[points[0], points[3]]));
                }

                let segments = (deviation_squared.sqrt() * 6.0 / (8.0 * PIXEL_ACCURACY))
                    .sqrt()
                    .ceil() as usize;
                let subdivisions = segments.max(1) - 1;

                if subdivisions == 0 {
                    return Err(edges.line(&[points[0], points[3]]));
                }

                Ok(subdivisions)
            },
            |t| {
                Point::lerp(
                    t,
                    Point::lerp(
                        t,
                        Point::lerp(t, points[0], points[1]),
                        Point::lerp(t, points[1], points[2]),
                    ),
                    Point::lerp(
                        t,
                        Point::lerp(t, points[1], points[2]),
                        Point::lerp(t, points[2], points[3]),
                    ),
                )
            },
        )
    }

    fn rat_cubic(&mut self, points: &[(Point<f32>, f32); 4]) -> Edge<f32> {
        let p0 = Self::from_weighted(points[0]);
        let p1 = Self::from_weighted(points[1]);
        let p2 = Self::from_weighted(points[2]);
        let p3 = Self::from_weighted(points[3]);

        self.increment(
            p0,
            p3,
            |edges| {
                let deviation_x =
                    max((p0.x + p2.x - 2.0 * p1.x).abs(), (p1.x + p3.x - 2.0 * p2.x).abs());
                let deviation_y =
                    max((p0.y + p2.y - 2.0 * p1.y).abs(), (p1.y + p3.y - 2.0 * p2.y).abs());
                let deviation_squared = deviation_x * deviation_x + deviation_y * deviation_y;

                if deviation_squared < PIXEL_ACCURACY {
                    return Err(edges.line(&[p0, p3]));
                }

                let segments = (deviation_squared.sqrt() * 6.0 / (8.0 * PIXEL_ACCURACY))
                    .sqrt()
                    .ceil() as usize;
                let subdivisions = segments.max(1) - 1;

                if subdivisions == 0 {
                    return Err(edges.line(&[p0, p3]));
                }

                Ok(subdivisions)
            },
            |t| {
                Point::lerp(
                    t,
                    Point::lerp(
                        t,
                        Point::lerp(t, points[0].0 * points[0].1, points[1].0 * points[1].1),
                        Point::lerp(t, points[1].0 * points[1].1, points[2].0 * points[2].1),
                    ),
                    Point::lerp(
                        t,
                        Point::lerp(t, points[1].0 * points[1].1, points[2].0 * points[2].1),
                        Point::lerp(t, points[2].0 * points[2].1, points[3].0 * points[3].1),
                    ),
                ) * lerp(
                    t,
                    lerp(t, lerp(t, points[0].1, points[1].1), lerp(t, points[1].1, points[2].1)),
                    lerp(t, lerp(t, points[1].1, points[2].1), lerp(t, points[2].1, points[3].1)),
                )
            },
        )
    }

    fn close(&mut self) -> Option<Edge<f32>> {
        let last_point = self.last_point;
        if let (Some(closing_point), Some(last_point)) = (self.closing_point, last_point) {
            let edge = if !closing_point.approx_eq(last_point) {
                Some(self.line(&[last_point, closing_point]))
            } else {
                None
            };

            self.closing_point = None;

            return edge;
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

impl Iterator for Edges<'_> {
    type Item = Edge<f32>;

    fn next(&mut self) -> Option<Self::Item> {
        if let Some(PathCommand::Close) = self.first() {
            self.commands = &self.commands[1..];

            return match self.close() {
                Some(edge) => Some(edge),
                None => self.next(),
            };
        }

        self.first().map(|command| {
            let edge = match command {
                PathCommand::Line(points) => self.line(&points),
                PathCommand::Quad(points) => self.quad(&points),
                PathCommand::RatQuad(points) => self.rat_quad(&points),
                PathCommand::Cubic(points) => self.cubic(&points),
                PathCommand::RatCubic(points) => self.rat_cubic(&points),
                _ => unimplemented!(),
            };

            if self.state.is_none() {
                self.commands = &self.commands[1..];
            }

            edge
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn close_open_path() {
        let mut path = Path::new();

        path.line(Point::new(0.0, 0.0), Point::new(1.0, 1.0));
        path.line(Point::new(1.0, 1.0), Point::new(2.0, 0.0));

        path.close();

        assert_eq!(
            path.edges().collect::<Vec<_>>(),
            vec![
                Edge::new(Point::new(0.0, 0.0), Point::new(1.0, 1.0)),
                Edge::new(Point::new(1.0, 1.0), Point::new(2.0, 0.0)),
                Edge::new(Point::new(2.0, 0.0), Point::new(0.0, 0.0)),
            ],
        );
    }

    #[test]
    fn close_closed_path() {
        let mut path = Path::new();

        path.line(Point::new(0.0, 0.0), Point::new(1.0, 1.0));
        path.line(Point::new(1.0, 1.0), Point::new(2.0, 0.0));
        path.line(Point::new(2.0, 0.0), Point::new(0.0, 0.0));

        path.close();

        assert_eq!(
            path.edges().collect::<Vec<_>>(),
            vec![
                Edge::new(Point::new(0.0, 0.0), Point::new(1.0, 1.0)),
                Edge::new(Point::new(1.0, 1.0), Point::new(2.0, 0.0)),
                Edge::new(Point::new(2.0, 0.0), Point::new(0.0, 0.0)),
            ],
        );
    }
    #[test]
    fn scale_translate_quad() {
        let transform = Transform::new(&[2.0, 0.0, 2.0, 0.0, 2.0, 2.0, 0.0, 0.0, 1.0]);
        let command =
            PathCommand::Quad([Point::new(0.0, 0.0), Point::new(1.0, 1.0), Point::new(2.0, 0.0)]);

        assert_eq!(
            transform.transform(command),
            PathCommand::Quad([Point::new(2.0, 2.0), Point::new(4.0, 4.0), Point::new(6.0, 2.0),])
        );
    }

    #[test]
    fn perspective_transform_quad() {
        let transform = Transform::new(&[1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.5, 0.0, 1.0]);
        let command =
            PathCommand::Quad([Point::new(0.0, 0.0), Point::new(1.0, 1.0), Point::new(2.0, 0.0)]);

        assert_eq!(
            transform.transform(command),
            PathCommand::RatQuad([
                (Point::new(0.0, 0.0), 1.0),
                (Point::new(1.0, 1.0), 1.5),
                (Point::new(2.0, 0.0), 2.0),
            ])
        );
    }

    #[test]
    fn scale_translate() {
        let mut path = Path::new();

        path.quad(Point::new(0.0, 0.0), Point::new(1.0, 1.0), Point::new(2.0, 0.0));

        let transformed: Vec<_> =
            path.transformed(&[2.0, 0.0, 2.0, 0.0, 2.0, 2.0, 0.0, 0.0, 1.0]).collect();

        assert_eq!(transformed.first().unwrap().p0, Point::new(2.0, 2.0));
        assert_eq!(transformed.last().unwrap().p1, Point::new(6.0, 2.0));
    }

    #[test]
    fn scale_translate_t22_not_1() {
        let mut path = Path::new();

        path.quad(Point::new(0.0, 0.0), Point::new(1.0, 1.0), Point::new(2.0, 0.0));

        let transformed: Vec<_> =
            path.transformed(&[2.0, 0.0, 2.0, 0.0, 2.0, 2.0, 0.0, 0.0, 2.0]).collect();

        assert_eq!(transformed.first().unwrap().p0, Point::new(1.0, 1.0));
        assert_eq!(transformed.last().unwrap().p1, Point::new(3.0, 1.0));
    }

    #[test]
    fn rational_bezier_with_weights_1() {
        let non_rational = Path {
            commands: vec![PathCommand::Quad([
                Point::new(2.0, 0.0),
                Point::new(2.0, 2.0),
                Point::new(0.0, 2.0),
            ])],
        };
        let rational = Path {
            commands: vec![PathCommand::RatQuad([
                (Point::new(2.0, 0.0), 1.0),
                (Point::new(2.0, 2.0), 1.0),
                (Point::new(0.0, 2.0), 1.0),
            ])],
        };

        assert_eq!(non_rational.edges().collect::<Vec<_>>(), rational.edges().collect::<Vec<_>>(),);
    }

    #[test]
    fn circle_rational_bezier() {
        const RADIUS: f32 = 10.0;

        let path = Path {
            commands: vec![PathCommand::RatQuad([
                (Point::new(RADIUS, 0.0), 1.0),
                (Point::new(RADIUS, RADIUS), 1.0 / RADIUS.sqrt()),
                (Point::new(0.0, RADIUS), 1.0),
            ])],
        };
        let edges: Vec<_> = path.edges().collect();

        for edge in &edges[1..] {
            assert!(
                edge.p0.x * edge.p0.x + edge.p0.y * edge.p0.y - RADIUS * RADIUS < std::f32::EPSILON
            );
        }
    }
}
