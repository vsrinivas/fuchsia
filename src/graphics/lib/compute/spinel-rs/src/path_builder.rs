// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, rc::Rc};

use crate::{
    context::{Context, ContextInner},
    path::Path,
    spinel_sys::*,
    Point,
};

#[derive(Clone, Debug)]
enum PathCommand {
    Move(Point),
    Line(Point),
    Quad([Point; 2]),
    QuadSmooth(Point),
    Cubic([Point; 3]),
    CubicSmooth([Point; 2]),
    RatQuad([Point; 2], f32),
    RatCubic([Point; 3], f32, f32),
}

/// Spinel `Path` builder.
///
/// Is actually a thin wrapper over the [spn_path_builder_t] stored in [`Context`].
///
/// [spn_path_builder_t]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#34
///
/// # Examples
///
/// ```no_run
/// # use spinel_rs::{Context, PathBuilder, Point};
/// #
/// # fn catch() -> Option<()> {
/// #     let context: Context = unimplemented!();
/// #
/// let tl = Point { x: 1.0, y: 1.0 };
/// let tr = Point { x: 5.0, y: 1.0 };
/// let br = Point { x: 5.0, y: 5.0 };
/// let bl = Point { x: 1.0, y: 5.0 };
///
/// let rectangle = PathBuilder::new(&context, tl)
///     .line_to(tr)
///     .line_to(br)
///     .line_to(bl)
///     .line_to(tl)
///     .build()?;
/// #     None
/// # }
/// ```
#[derive(Clone, Debug)]
pub struct PathBuilder {
    context: Rc<RefCell<ContextInner>>,
    cmds: Vec<PathCommand>,
}

macro_rules! panic_if_not_finite {
    ( $point:expr ) => {
        if !$point.is_finite() {
            panic!("{:?} does not have finite f32 values", $point);
        }
    };
}

impl PathBuilder {
    /// Creates a new path builder that keeps track of an `end-point` which is the last point of the
    /// path used in subsequent calls.
    ///
    /// `start_point` is the first `end-point`. [spn_path_move_to]
    ///
    /// [spn_path_move_to]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#73
    pub fn new(context: &Context, start_point: Point) -> Self {
        Self { context: Rc::clone(&context.inner), cmds: vec![PathCommand::Move(start_point)] }
    }

    /// Adds line from `end-point` to `point`. [spn_path_line_to]
    ///
    /// [spn_path_line_to]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#77
    pub fn line_to(mut self, point: Point) -> Self {
        panic_if_not_finite!(point);

        self.cmds.push(PathCommand::Line(point));
        self
    }

    /// Adds quadratic Bézier from `end-point` to `point[1]` with `point[0]` as a control point.
    /// [spn_path_quad_to]
    ///
    /// [spn_path_quad_to]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#92
    pub fn quad_to(mut self, points: [Point; 2]) -> Self {
        for point in &points {
            panic_if_not_finite!(point);
        }

        self.cmds.push(PathCommand::Quad(points));
        self
    }

    /// Adds smooth quadratic Bézier from `end-point` to `point`. [spn_path_quad_smooth_to]
    ///
    /// [spn_path_quad_smooth_to]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#97
    pub fn quad_smooth_to(mut self, point: Point) -> Self {
        panic_if_not_finite!(point);

        self.cmds.push(PathCommand::QuadSmooth(point));
        self
    }

    /// Adds cubic Bézier from `end-point` to `point[2]` with `point[0]` and `point[1]` as control
    /// points. [spn_path_cubic_to]
    ///
    /// [spn_path_cubic_to]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#81
    pub fn cubic_to(mut self, points: [Point; 3]) -> Self {
        for point in &points {
            panic_if_not_finite!(point);
        }

        self.cmds.push(PathCommand::Cubic(points));
        self
    }

    /// Adds smooth cubic Bézier from `end-point` to `point[1]` with `point[0]` as control point.
    /// [spn_path_cubic_smooth_to]
    ///
    /// [spn_path_cubic_smooth_to]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#87
    pub fn cubic_smooth_to(mut self, points: [Point; 2]) -> Self {
        for point in &points {
            panic_if_not_finite!(point);
        }

        self.cmds.push(PathCommand::CubicSmooth(points));
        self
    }

    /// Adds rational quadratic Bézier from `end-point` to `point[1]` with `point[0]` as a control
    /// point and `w0` as weight. [spn_path_rat_quad_to]
    ///
    /// [spn_path_rat_quad_to]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#101
    pub fn rat_quad_to(mut self, points: [Point; 2], w0: f32) -> Self {
        for point in &points {
            panic_if_not_finite!(point);
        }

        if !w0.is_finite() {
            panic!("{} (w0) is not finite", w0);
        }

        self.cmds.push(PathCommand::RatQuad(points, w0));
        self
    }

    /// Adds rational cubic Bézier from `end-point` to `point[2]` with `point[0]` and `point[1]` as
    /// control points, and `w0` and `w1` as weights. [spn_path_rat_cubic_to]
    ///
    /// [spn_path_rat_cubic_to]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#107
    pub fn rat_cubic_to(mut self, points: [Point; 3], w0: f32, w1: f32) -> Self {
        for point in &points {
            panic_if_not_finite!(point);
        }

        if !w0.is_finite() {
            panic!("{} (w0) is not finite", w0);
        }

        if !w1.is_finite() {
            panic!("{} (w1) is not finite", w1);
        }

        self.cmds.push(PathCommand::RatCubic(points, w0, w1));
        self
    }

    fn end_point(&self) -> Point {
        match self.cmds.last().expect("PathBuilder should always be initialized with Move") {
            PathCommand::Move(p) => *p,
            PathCommand::Line(p) => *p,
            PathCommand::Quad(points) => points[1],
            PathCommand::QuadSmooth(p) => *p,
            PathCommand::Cubic(points) => points[2],
            PathCommand::CubicSmooth(points) => points[1],
            PathCommand::RatQuad(points, ..) => points[1],
            PathCommand::RatCubic(points, ..) => points[2],
        }
    }

    /// Builds `Path`. Calls [spn_path_begin] and [spn_path_end] to allocate the path.
    ///
    /// If the path is not closed, a straight line will be added to connect the end-point back to
    /// the start-point.
    ///
    /// If there is not enough memory to allocate the path, `None` is returned instead.
    ///
    /// [spn_path_begin]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#55
    /// [spn_path_end]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#58
    pub fn build(mut self) -> Option<Path> {
        macro_rules! success {
            ( $result:expr, $path_builder:expr $( , )? ) => {{
                if let Err(SpnError::SpnErrorPathBuilderLost) = $result.res() {
                    $path_builder.context.borrow_mut().reset_path_builder();
                    return None;
                }
                $result.success();
            }};
        }

        unsafe {
            let spn_path_builder = self.context.borrow().spn_path_builder;

            success!(spn_path_begin(spn_path_builder), self);

            let start_point = match self.cmds[0] {
                PathCommand::Move(p) => p,
                _ => panic!("PathBuilder should always be initialized with Move"),
            };
            let end_point = self.end_point();

            // If path is not closed, close it with a line.
            if !start_point.approx_eq(end_point) {
                self.cmds.push(PathCommand::Line(start_point));
            }

            for cmd in self.cmds {
                match cmd {
                    PathCommand::Move(p) => {
                        success!(spn_path_move_to(spn_path_builder, p.x, p.y), self,)
                    }
                    PathCommand::Line(p) => {
                        success!(spn_path_line_to(spn_path_builder, p.x, p.y), self,)
                    }
                    PathCommand::Quad([p1, p2]) => {
                        success!(spn_path_quad_to(spn_path_builder, p1.x, p1.y, p2.x, p2.y), self,)
                    }
                    PathCommand::QuadSmooth(p) => {
                        success!(spn_path_quad_smooth_to(spn_path_builder, p.x, p.y), self,)
                    }
                    PathCommand::Cubic([p1, p2, p3]) => success!(
                        spn_path_cubic_to(spn_path_builder, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y),
                        self,
                    ),
                    PathCommand::CubicSmooth([p2, p3]) => success!(
                        spn_path_cubic_smooth_to(spn_path_builder, p2.x, p2.y, p3.x, p3.y),
                        self,
                    ),
                    PathCommand::RatQuad([p1, p2], w0) => success!(
                        spn_path_rat_quad_to(spn_path_builder, p1.x, p1.y, p2.x, p2.y, w0),
                        self,
                    ),
                    PathCommand::RatCubic([p1, p2, p3], w0, w1) => success!(
                        spn_path_rat_cubic_to(
                            spn_path_builder,
                            p1.x,
                            p1.y,
                            p2.x,
                            p2.y,
                            p3.x,
                            p3.y,
                            w0,
                            w1,
                        ),
                        self,
                    ),
                }
            }

            let mut spn_path = Default::default();
            success!(spn_path_end(spn_path_builder, &mut spn_path as *mut _), self);

            Some(Path::new(&self.context, spn_path))
        }
    }
}

#[cfg(test)]
mod tests {
    use std::f32;

    use super::*;

    const FINITE: Point = Point { x: 0.0, y: 1.0 };
    const X_NAN: Point = Point { x: f32::NAN, y: 1.0 };
    const Y_INF: Point = Point { x: 0.0, y: f32::INFINITY };

    fn new_path_builder() -> PathBuilder {
        let context = Context::new();
        PathBuilder::new(&context, Point::default())
    }

    #[test]
    fn point_is_finite() {
        assert!(FINITE.is_finite());
        assert!(!X_NAN.is_finite());
        assert!(!Y_INF.is_finite());
    }

    #[test]
    fn line_to_finite() {
        let path_builder = new_path_builder();
        path_builder.line_to(FINITE);
    }

    #[test]
    #[should_panic]
    fn line_to_non_finite() {
        let path_builder = new_path_builder();
        path_builder.line_to(X_NAN);
    }

    #[test]
    fn quad_to_finite() {
        let path_builder = new_path_builder();
        path_builder.quad_to([FINITE, FINITE]);
    }

    #[test]
    #[should_panic]
    fn quad_to_non_finite() {
        let path_builder = new_path_builder();
        path_builder.quad_to([X_NAN, FINITE]);
    }

    #[test]
    fn quad_smooth_to_finite() {
        let path_builder = new_path_builder();
        path_builder.quad_smooth_to(FINITE);
    }

    #[test]
    #[should_panic]
    fn quad_smooth_to_non_finite() {
        let path_builder = new_path_builder();
        path_builder.quad_smooth_to(X_NAN);
    }

    #[test]
    fn cubic_to_finite() {
        let path_builder = new_path_builder();
        path_builder.cubic_to([FINITE, FINITE, FINITE]);
    }

    #[test]
    #[should_panic]
    fn cubic_to_non_finite() {
        let path_builder = new_path_builder();
        path_builder.cubic_to([X_NAN, FINITE, FINITE]);
    }

    #[test]
    fn cubic_smooth_to_finite() {
        let path_builder = new_path_builder();
        path_builder.cubic_smooth_to([FINITE, FINITE]);
    }

    #[test]
    #[should_panic]
    fn cubic_smooth_to_non_finite() {
        let path_builder = new_path_builder();
        path_builder.cubic_smooth_to([X_NAN, FINITE]);
    }

    #[test]
    fn rat_quad_to_finite() {
        let path_builder = new_path_builder();
        path_builder.rat_quad_to([FINITE, FINITE], 0.0);
    }

    #[test]
    #[should_panic]
    fn rat_quad_to_non_finite_points() {
        let path_builder = new_path_builder();
        path_builder.rat_quad_to([X_NAN, FINITE], 0.0);
    }

    #[test]
    #[should_panic]
    fn rat_quad_to_non_finite_w0() {
        let path_builder = new_path_builder();
        path_builder.rat_quad_to([FINITE, FINITE], f32::NAN);
    }

    #[test]
    fn rat_cubic_to_finite() {
        let path_builder = new_path_builder();
        path_builder.rat_cubic_to([FINITE, FINITE, FINITE], 0.0, 0.0);
    }

    #[test]
    #[should_panic]
    fn rat_cubic_to_non_finite_points() {
        let path_builder = new_path_builder();
        path_builder.rat_cubic_to([X_NAN, FINITE, FINITE], 0.0, 0.0);
    }

    #[test]
    #[should_panic]
    fn rat_cubic_to_non_finite_w0() {
        let path_builder = new_path_builder();
        path_builder.rat_cubic_to([FINITE, FINITE, FINITE], f32::NAN, 0.0);
    }

    #[test]
    #[should_panic]
    fn rat_cubic_to_non_finite_w1() {
        let path_builder = new_path_builder();
        path_builder.rat_cubic_to([FINITE, FINITE, FINITE], 0.0, f32::NAN);
    }
}
