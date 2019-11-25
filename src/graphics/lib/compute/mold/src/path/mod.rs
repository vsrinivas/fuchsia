// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::point::Point;

mod segments;
mod transform;

use segments::PathSegments;
use transform::Transform;

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) enum PathCommand {
    Line([Point<f32>; 2]),
    Quad([Point<f32>; 3]),
    RatQuad([(Point<f32>, f32); 3]),
    Cubic([Point<f32>; 4]),
    RatCubic([(Point<f32>, f32); 4]),
    Close,
}

/// A vector path containing one contour.
#[derive(Clone, Debug, Default)]
pub struct Path {
    commands: Vec<PathCommand>,
}

impl Path {
    /// Creates a new path.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::Path;
    /// let path = Path::new();
    /// ```
    pub fn new() -> Self {
        Self { commands: vec![] }
    }

    /// Adds a line to the path from `p0` to `p1`.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Path, Point};
    /// let mut path = Path::new();
    /// path.line(
    ///     Point::new(1.0, 2.0),
    ///     Point::new(1.0, 3.0),
    /// );
    /// ```
    pub fn line(&mut self, p0: Point<f32>, p1: Point<f32>) {
        self.commands.push(PathCommand::Line([p0, p1]));
    }

    /// Adds a quadratic Bézier from `p0` to `p2` with `p1` as control point.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Path, Point};
    /// let mut path = Path::new();
    /// path.quad(
    ///     Point::new(1.0, 2.0),
    ///     Point::new(1.0, 3.0),
    ///     Point::new(1.0, 4.0),
    /// );
    /// ```
    pub fn quad(&mut self, p0: Point<f32>, p1: Point<f32>, p2: Point<f32>) {
        self.commands.push(PathCommand::Quad([p0, p1, p2]));
    }

    /// Adds a rational quadratic Bézier from `p0` to `p2` with `p1` as control point. Each weighted
    /// point is represented by a tuple contining its point and its weight.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Path, Point};
    /// let mut path = Path::new();
    /// path.rat_quad(
    ///     (Point::new(1.0, 2.0), 1.0),
    ///     (Point::new(1.0, 3.0), 1.0),
    ///     (Point::new(1.0, 4.0), 1.0),
    /// );
    /// ```
    pub fn rat_quad(
        &mut self,
        p0: (Point<f32>, f32),
        p1: (Point<f32>, f32),
        p2: (Point<f32>, f32),
    ) {
        self.commands.push(PathCommand::RatQuad([p0, p1, p2]));
    }

    /// Adds a cubic Bézier from `p0` to `p3` with `p1` and `p2` as control points.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Path, Point};
    /// let mut path = Path::new();
    /// path.cubic(
    ///     Point::new(1.0, 2.0),
    ///     Point::new(1.0, 3.0),
    ///     Point::new(1.0, 4.0),
    ///     Point::new(1.0, 5.0),
    /// );
    /// ```
    pub fn cubic(&mut self, p0: Point<f32>, p1: Point<f32>, p2: Point<f32>, p3: Point<f32>) {
        self.commands.push(PathCommand::Cubic([p0, p1, p2, p3]));
    }

    /// Adds a rational cubic Bézier from `p0` to `p3` with `p1` and `p2` as control points. Each
    /// weighted point is represented by a tuple contining its point and its weight.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Path, Point};
    /// let mut path = Path::new();
    /// path.rat_cubic(
    ///     (Point::new(1.0, 2.0), 1.0),
    ///     (Point::new(1.0, 3.0), 1.0),
    ///     (Point::new(1.0, 4.0), 1.0),
    ///     (Point::new(1.0, 5.0), 1.0),
    /// );
    /// ```
    pub fn rat_cubic(
        &mut self,
        p0: (Point<f32>, f32),
        p1: (Point<f32>, f32),
        p2: (Point<f32>, f32),
        p3: (Point<f32>, f32),
    ) {
        self.commands.push(PathCommand::RatCubic([p0, p1, p2, p3]));
    }

    /// Closes the contour defined by the path. If the starting point and ending point of the
    /// contour don't match, a straight line is added between them that closes the contour.
    ///
    /// # Examples
    /// ```
    /// # use crate::mold::{Path, Point};
    /// let mut path = Path::new();
    /// path.line(
    ///     Point::new(0.0, 0.0),
    ///     Point::new(1.0, 1.0),
    /// );
    /// path.line(
    ///     Point::new(1.0, 1.0),
    ///     Point::new(2.0, 0.0),
    /// );
    ///
    /// path.close();
    /// // Equivalent to:
    /// // path.line(
    /// //     Point::new(2.0, 0.0),
    /// //     Point::new(0.0, 0.0),
    /// // );
    /// ```
    pub fn close(&mut self) {
        self.commands.push(PathCommand::Close);
    }

    pub(crate) fn segments(&self) -> PathSegments<'_> {
        PathSegments::new(&self.commands, None)
    }

    pub(crate) fn transformed(&self, transform: &[f32; 9]) -> PathSegments<'_> {
        PathSegments::new(&self.commands, Some(Transform::new(transform)))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::segment::Segment;

    #[test]
    fn path_interface() {
        let mut path = Path::new();

        path.line(Point::new(1.0, 1.0), Point::new(1.0, 1.0));
        assert_eq!(
            path.commands.last(),
            Some(&PathCommand::Line([Point::new(1.0, 1.0), Point::new(1.0, 1.0),]))
        );

        path.quad(Point::new(1.0, 1.0), Point::new(1.0, 1.0), Point::new(1.0, 1.0));
        assert_eq!(
            path.commands.last(),
            Some(&PathCommand::Quad([
                Point::new(1.0, 1.0),
                Point::new(1.0, 1.0),
                Point::new(1.0, 1.0),
            ]))
        );

        path.rat_quad(
            (Point::new(1.0, 1.0), 1.0),
            (Point::new(1.0, 1.0), 1.0),
            (Point::new(1.0, 1.0), 1.0),
        );
        assert_eq!(
            path.commands.last(),
            Some(&PathCommand::RatQuad([
                (Point::new(1.0, 1.0), 1.0),
                (Point::new(1.0, 1.0), 1.0),
                (Point::new(1.0, 1.0), 1.0),
            ]))
        );

        path.cubic(
            Point::new(1.0, 1.0),
            Point::new(1.0, 1.0),
            Point::new(1.0, 1.0),
            Point::new(1.0, 1.0),
        );
        assert_eq!(
            path.commands.last(),
            Some(&PathCommand::Cubic([
                Point::new(1.0, 1.0),
                Point::new(1.0, 1.0),
                Point::new(1.0, 1.0),
                Point::new(1.0, 1.0),
            ]))
        );

        path.rat_cubic(
            (Point::new(1.0, 1.0), 1.0),
            (Point::new(1.0, 1.0), 1.0),
            (Point::new(1.0, 1.0), 1.0),
            (Point::new(1.0, 1.0), 1.0),
        );
        assert_eq!(
            path.commands.last(),
            Some(&PathCommand::RatCubic([
                (Point::new(1.0, 1.0), 1.0),
                (Point::new(1.0, 1.0), 1.0),
                (Point::new(1.0, 1.0), 1.0),
                (Point::new(1.0, 1.0), 1.0),
            ]))
        );
    }

    #[test]
    fn close_open_path() {
        let mut path = Path::new();

        path.line(Point::new(0.0, 0.0), Point::new(1.0, 1.0));
        path.line(Point::new(1.0, 1.0), Point::new(2.0, 0.0));

        path.close();

        assert_eq!(
            path.segments().collect::<Vec<_>>(),
            vec![
                Segment::new(Point::new(0.0, 0.0), Point::new(1.0, 1.0)),
                Segment::new(Point::new(1.0, 1.0), Point::new(2.0, 0.0)),
                Segment::new(Point::new(2.0, 0.0), Point::new(0.0, 0.0)),
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
            path.segments().collect::<Vec<_>>(),
            vec![
                Segment::new(Point::new(0.0, 0.0), Point::new(1.0, 1.0)),
                Segment::new(Point::new(1.0, 1.0), Point::new(2.0, 0.0)),
                Segment::new(Point::new(2.0, 0.0), Point::new(0.0, 0.0)),
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

        assert_eq!(
            non_rational.segments().collect::<Vec<_>>(),
            rational.segments().collect::<Vec<_>>(),
        );
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
        let segments: Vec<_> = path.segments().collect();

        for segment in &segments[1..] {
            assert!(
                segment.p0.x * segment.p0.x + segment.p0.y * segment.p0.y - RADIUS * RADIUS
                    < std::f32::EPSILON
            );
        }
    }

    #[test]
    fn quad_split_into_segments() {
        let mut path = Path::new();

        path.quad(Point::new(0.0, 0.0), Point::new(1.0, 1.0), Point::new(2.0, 0.0));

        assert_eq!(path.segments().count(), 3);
    }
}
