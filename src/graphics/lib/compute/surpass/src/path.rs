// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The path division algorithm is mostly based on Raph Levien's curvature approximation[1] for
// quadratic Bézier division and Adrian Colomitchi's cubic-to-quadratic approximation[2] with a few
// additions to improve robustness.
//
// The algorithm converts all possible types of curves into primitives (lines and quadratic
// Béziers) sequentially, while these are pushed onto the `PathBuilder`. Afterwards, each `Path`
// is converted into lines in parallel. This part of the algorithms tries its best not to create
// new lines if two neighboring lines are close enough to forming a line together according to some
// threshold.
//
// [1]: https://raphlinus.github.io/graphics/curves/2019/12/23/flatten-quadbez.html
// [2]: http://www.caffeineowl.com/graphics/2d/vectorial/bezierintro.html

use std::{cell::RefCell, f32, ops::Sub, rc::Rc};

use rayon::prelude::*;

use crate::{extend::ExtendVec, Point, PIXEL_WIDTH};

// Pixel accuracy should be within 0.5 of a sub-pixel.
const MAX_ERROR: f32 = 0.5 / PIXEL_WIDTH as f32;
const MAX_ANGLE_ERROR: f32 = 0.001;
const MIN_LEN: usize = 256;

fn lerp(t: f32, a: f32, b: f32) -> f32 {
    t.mul_add(b, (-t).mul_add(a, a))
}

fn curvature(x: f32) -> f32 {
    const C: f32 = 0.67;
    x / (1.0 - C + ((x * x).mul_add(0.25, C * C * C * C)).sqrt().sqrt())
}

fn inv_curvature(k: f32) -> f32 {
    const C: f32 = 0.39;
    k * (1.0 - C + ((k * k).mul_add(0.25, C * C))).sqrt()
}

#[derive(Clone, Copy, Debug)]
pub struct WeightedPoint {
    pub point: Point<f32>,
    pub weight: f32,
}

impl WeightedPoint {
    pub fn applied(self) -> Point<f32> {
        let w_recip = self.weight.recip();

        Point { x: self.point.x * w_recip, y: self.point.y * w_recip }
    }
}

fn eval_cubic(t: f32, points: &[WeightedPoint; 4]) -> WeightedPoint {
    let x = lerp(
        t,
        lerp(
            t,
            lerp(t, points[0].point.x * points[0].weight, points[1].point.x * points[1].weight),
            lerp(t, points[1].point.x * points[1].weight, points[2].point.x * points[2].weight),
        ),
        lerp(
            t,
            lerp(t, points[1].point.x * points[1].weight, points[2].point.x * points[2].weight),
            lerp(t, points[2].point.x * points[2].weight, points[3].point.x * points[3].weight),
        ),
    );
    let y = lerp(
        t,
        lerp(
            t,
            lerp(t, points[0].point.y * points[0].weight, points[1].point.y * points[1].weight),
            lerp(t, points[1].point.y * points[1].weight, points[2].point.y * points[2].weight),
        ),
        lerp(
            t,
            lerp(t, points[1].point.y * points[1].weight, points[2].point.y * points[2].weight),
            lerp(t, points[2].point.y * points[2].weight, points[3].point.y * points[3].weight),
        ),
    );
    let weight = lerp(
        t,
        lerp(
            t,
            lerp(t, points[0].weight, points[1].weight),
            lerp(t, points[1].weight, points[2].weight),
        ),
        lerp(
            t,
            lerp(t, points[1].weight, points[2].weight),
            lerp(t, points[2].weight, points[3].weight),
        ),
    );

    WeightedPoint { point: Point { x, y }, weight }
}

#[derive(Debug, Default)]
pub struct ScratchBuffers {
    point_indices: Vec<usize>,
    quad_indices: Vec<usize>,
    point_commands: Vec<u32>,
}

impl ScratchBuffers {
    pub fn clear(&mut self) {
        self.point_indices.clear();
        self.quad_indices.clear();
        self.point_commands.clear();
    }
}

#[derive(Clone, Copy, Debug)]
enum PointCommand {
    Start(usize),
    Incr(f32),
    End(usize, bool),
}

impl From<u32> for PointCommand {
    fn from(val: u32) -> Self {
        if val & 0x7F80_0000 == 0x7F80_0000 {
            if val & 0x8000_0000 == 0 {
                Self::Start((val & 0x3F_FFFF) as usize)
            } else {
                Self::End((val & 0x3F_FFFF) as usize, val & 0x40_0000 != 0)
            }
        } else {
            Self::Incr(f32::from_bits(val))
        }
    }
}

impl Into<u32> for PointCommand {
    fn into(self) -> u32 {
        match self {
            Self::Start(i) => 0x7F80_0000 | (i as u32 & 0x3F_FFFF),
            Self::Incr(point_command) => point_command.to_bits(),
            Self::End(i, new_contour) => {
                0xFF80_0000 | (i as u32 & 0x3F_FFFF) | ((new_contour as u32) << 22)
            }
        }
    }
}

impl Point<f32> {
    pub fn len(self) -> f32 {
        (self.x * self.x + self.y * self.y).sqrt()
    }

    pub fn angle(self) -> Option<f32> {
        (self.len() >= f32::EPSILON).then(|| self.y.atan2(self.x))
    }
}

impl Sub for Point<f32> {
    type Output = Self;

    fn sub(self, rhs: Self) -> Self::Output {
        Self { x: self.x - rhs.x, y: self.y - rhs.y }
    }
}

#[derive(Clone, Debug)]
struct Contour;

#[derive(Clone, Debug)]
struct Spline {
    angle: Option<f32>,
    curvature: f32,
    p0: Point<f32>,
    p2: Point<f32>,
    contour: Option<Contour>,
}

impl Spline {
    pub fn new_spline_needed(&mut self, angle: Option<f32>, point: Point<f32>) -> Option<Contour> {
        fn diff(a0: f32, a1: f32) -> f32 {
            let mut diff = (a1 - a0).abs() % f32::consts::PI;

            if diff > f32::consts::FRAC_PI_2 {
                diff = f32::consts::PI - diff;
            }

            diff
        }

        let angle_changed = if let (Some(a0), Some(a1)) = (self.angle, angle) {
            diff(a0, a1) > MAX_ANGLE_ERROR
        } else {
            false
        };

        let needed = angle_changed || (point - self.p2).len() >= MAX_ERROR;

        needed.then(|| self.contour.take()).flatten()
    }
}

#[derive(Clone, Debug)]
struct Primitives {
    contour: Option<Contour>,
    splines: Vec<Spline>,
    x: Vec<f32>,
    y: Vec<f32>,
    weight: Vec<f32>,
    x0: Vec<f32>,
    dx: Vec<f32>,
    k0: Vec<f32>,
    dk: Vec<f32>,
    curvatures_recip: Vec<f32>,
    partial_curvatures: Vec<(u32, f32)>,
}

impl Primitives {
    fn last_spline_or_insert_with<F>(
        &mut self,
        angle: Option<f32>,
        point: Point<f32>,
        f: F,
    ) -> &mut Spline
    where
        F: FnOnce(Contour) -> Spline,
    {
        if let Some(contour) = self.contour.take().or_else(|| {
            self.splines.last_mut().and_then(|spline| spline.new_spline_needed(angle, point))
        }) {
            self.splines.push(f(contour));
        }

        self.splines.last_mut().unwrap()
    }

    pub fn push_contour(&mut self, contour: Contour) {
        self.contour = Some(contour);
    }

    pub fn push_line(&mut self, points: [WeightedPoint; 2]) {
        let p0 = points[0].applied();
        let p1 = points[1].applied();

        let d = p1 - p0;
        let angle = d.angle();

        let spline = self.last_spline_or_insert_with(angle, p0, |contour| Spline {
            angle,
            curvature: 0.0,
            p0,
            p2: p1,
            contour: Some(contour),
        });

        spline.p2 = p1;
    }

    pub fn push_quad(&mut self, points: [WeightedPoint; 3]) {
        const PIXEL_ACCURACY_RECIP: f32 = 1.0 / MAX_ERROR;

        let p0 = points[0].applied();
        let p1 = points[1].point;
        let p2 = points[2].applied();

        let a = p1 - p0;
        let b = p2 - p1;

        let in_angle = a.angle();
        let out_angle = b.angle();

        if in_angle.is_none() && out_angle.is_none() {
            return;
        }

        if in_angle.is_none() || out_angle.is_none() {
            return self.push_line([points[0], points[2]]);
        }

        self.x.extend(points.iter().map(|p| p.point.x));
        self.y.extend(points.iter().map(|p| p.point.y));
        self.weight.extend(points.iter().map(|p| p.weight));

        let spline = self.last_spline_or_insert_with(in_angle, p0, |contour| Spline {
            angle: out_angle,
            curvature: 0.0,
            p0,
            p2,
            contour: Some(contour),
        });

        spline.angle = out_angle;
        spline.p2 = p2;

        let h = a - b;

        let cross = (p2.x - p0.x).mul_add(h.y, -(p2.y - p0.y) * h.x);
        let cross_recip = cross.recip();

        let mut x0 = a.x.mul_add(h.x, a.y * h.y) * cross_recip;
        let x2 = b.x.mul_add(h.x, b.y * h.y) * cross_recip;
        let mut dx = x2 - x0;

        let scale = (cross / (h.len() * (x2 - x0))).abs();

        let mut k0 = curvature(x0);
        let k2 = curvature(x2);

        let mut dk = k2 - k0;
        let mut current_curvature = 0.5 * dk.abs() * (scale * PIXEL_ACCURACY_RECIP).sqrt();

        // Points are collinear.
        if !current_curvature.is_finite() || current_curvature <= 1.0 {
            // These values are chosen such that the resulting points will be found at t = 0.5 and
            // t = 1.0.
            x0 = -0.091925144;
            dx = 1.091925144;
            k0 = 0.0;
            dk = 1.0;

            current_curvature = 2.0;
        }

        let total_curvature = spline.curvature + current_curvature;

        spline.curvature = total_curvature;

        self.x0.push(x0);
        self.dx.push(dx);
        self.k0.push(k0);
        self.dk.push(dk);
        self.curvatures_recip.push(current_curvature.recip());
        self.partial_curvatures.push((self.splines.len() as u32 - 1, total_curvature));
    }

    pub fn push_cubic(&mut self, points: [WeightedPoint; 4]) {
        const MAX_CUBIC_ERROR_SQUARED: f32 = (36.0 * 36.0 / 3.0) * MAX_ERROR * MAX_ERROR;

        let p0 = points[0].applied();
        let p1 = points[1].point;
        let p2 = points[2].point;

        let dx = p2.x.mul_add(3.0, -p0.x) - p1.x.mul_add(3.0, -p1.x);
        let dy = p2.y.mul_add(3.0, -p0.y) - p1.y.mul_add(3.0, -p1.y);

        let err = dx.mul_add(dx, dy * dy);

        let mult = points[1].weight.max(points[2].weight).max(1.0);

        let subdivisions = (((err * MAX_CUBIC_ERROR_SQUARED.recip()).powf(1.0 / 6.0) * mult).ceil()
            as usize)
            .max(1);
        let incr = (subdivisions as f32).recip();

        let mut quad_p0 = p0;
        for i in 1..=subdivisions {
            let t = i as f32 * incr;

            let quad_p2 = eval_cubic(t, &points).applied();

            let mid_point = eval_cubic(t - 0.5 * incr, &points).applied();

            let quad_p1 = Point {
                x: mid_point.x.mul_add(2.0, -0.5 * (quad_p0.x + quad_p2.x)),
                y: mid_point.y.mul_add(2.0, -0.5 * (quad_p0.y + quad_p2.y)),
            };

            self.push_quad([
                WeightedPoint { point: quad_p0, weight: 1.0 },
                WeightedPoint { point: quad_p1, weight: 1.0 },
                WeightedPoint { point: quad_p2, weight: 1.0 },
            ]);

            quad_p0 = quad_p2;
        }
    }

    pub fn push_lines_to(
        &self,
        x: &mut Vec<f32>,
        y: &mut Vec<f32>,
        layer_id: u32,
        layer_ids: &mut Vec<Option<u32>>,
        buffers: &mut ScratchBuffers,
    ) {
        buffers.point_indices.clear();
        buffers.quad_indices.clear();
        buffers.point_commands.clear();

        let mut i = 0;
        let mut last_spline = None;
        for (spline_i, spline) in self.splines.iter().enumerate() {
            let subdivisions = spline.curvature.ceil() as usize;
            let point_command = spline.curvature / subdivisions as f32;

            let needs_start_point = last_spline
                .map(|last_spline: &Spline| {
                    last_spline.contour.is_some() || (last_spline.p2 - spline.p0).len() > MAX_ERROR
                })
                .unwrap_or(true);

            if needs_start_point {
                buffers.point_indices.push(Default::default());
                buffers.quad_indices.push(Default::default());
                buffers.point_commands.push(PointCommand::Start(spline_i).into());
            }

            for pi in 1..subdivisions {
                if pi as f32 > self.partial_curvatures[i].1 {
                    i += 1;
                }

                buffers.point_indices.push(pi);
                buffers.quad_indices.push(i);
                buffers.point_commands.push(PointCommand::Incr(point_command).into());
            }

            buffers.point_indices.push(Default::default());
            buffers.quad_indices.push(Default::default());
            buffers
                .point_commands
                .push(PointCommand::End(spline_i, spline.contour.is_some()).into());

            last_spline = Some(spline);

            if subdivisions > 0 {
                i += 1;
            }
        }

        let points = buffers
            .point_indices
            .par_iter()
            .with_min_len(MIN_LEN)
            .zip(buffers.quad_indices.par_iter().with_min_len(MIN_LEN))
            .zip(buffers.point_commands.par_iter().with_min_len(MIN_LEN))
            .map(|((&pi, &qi), &point_command)| {
                let incr = match PointCommand::from(point_command) {
                    PointCommand::Start(spline_i) => {
                        let point = self.splines[spline_i as usize].p0;
                        return ((point.x, point.y), Some(layer_id));
                    }
                    PointCommand::End(spline_i, new_contour) => {
                        let point = self.splines[spline_i as usize].p2;
                        return ((point.x, point.y), (!new_contour).then(|| layer_id));
                    }
                    PointCommand::Incr(incr) => incr,
                };

                let i0 = 3 * qi;
                let i1 = i0 + 1;
                let i2 = i0 + 2;

                let spline_i = self.partial_curvatures[qi].0;

                let previous_curvature = qi
                    .checked_sub(1)
                    .and_then(|i| {
                        let partial_curvature = self.partial_curvatures[i];
                        (partial_curvature.0 == spline_i).then(|| partial_curvature.1)
                    })
                    .unwrap_or_default();
                let ratio =
                    incr.mul_add(pi as f32, -previous_curvature) * self.curvatures_recip[qi];

                let x = inv_curvature(ratio.mul_add(self.dk[qi], self.k0[qi]));

                let t = ((x - self.x0[qi]) / self.dx[qi]).clamp(0.0, 1.0);

                let weight = lerp(
                    t,
                    lerp(t, self.weight[i0], self.weight[i1]),
                    lerp(t, self.weight[i1], self.weight[i2]),
                );
                let w_recip = weight.recip();

                let x = lerp(
                    t,
                    lerp(t, self.x[i0] * self.weight[i0], self.x[i1] * self.weight[i1]),
                    lerp(t, self.x[i1] * self.weight[i1], self.x[i2] * self.weight[i2]),
                ) * w_recip;
                let y = lerp(
                    t,
                    lerp(t, self.y[i0] * self.weight[i0], self.y[i1] * self.weight[i1]),
                    lerp(t, self.y[i1] * self.weight[i1], self.y[i2] * self.weight[i2]),
                ) * w_recip;

                ((x, y), Some(layer_id))
            });

        ((ExtendVec::new(x), ExtendVec::new(y)), ExtendVec::new(layer_ids)).par_extend(points);
    }
}

impl Default for Primitives {
    fn default() -> Self {
        Self {
            contour: Some(Contour),
            splines: Default::default(),
            x: Default::default(),
            y: Default::default(),
            weight: Default::default(),
            x0: Default::default(),
            dx: Default::default(),
            k0: Default::default(),
            dk: Default::default(),
            curvatures_recip: Default::default(),
            partial_curvatures: Default::default(),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
enum PathCommand {
    MoveTo,
    LineTo,
    QuadTo,
    CubicTo,
}

#[derive(Clone, Debug)]
struct PathData {
    x: Vec<f32>,
    y: Vec<f32>,
    weight: Vec<f32>,
    commands: Vec<PathCommand>,
    open_point_index: usize,
    primitives: Primitives,
}

macro_rules! points {
    ( $inner:expr , $i:expr , $( $d:expr ),+ $( , )? ) => {[
        $(
            WeightedPoint {
                point: Point::new($inner.x[$i - $d], $inner.y[$i - $d]),
                weight: $inner.weight[$i - $d],
            },
        )*
    ]};
}

impl PathData {
    pub fn close(&mut self) {
        let len = self.x.len();

        let last_point = WeightedPoint {
            point: Point::new(self.x[len - 1], self.y[len - 1]),
            weight: self.weight[len - 1],
        };
        let open_point = WeightedPoint {
            point: Point::new(self.x[self.open_point_index], self.y[self.open_point_index]),
            weight: self.weight[self.open_point_index],
        };

        if (last_point.applied() - open_point.applied()).len() >= MAX_ERROR {
            self.x.push(open_point.point.x);
            self.y.push(open_point.point.y);
            self.weight.push(open_point.weight);

            self.commands.push(PathCommand::LineTo);

            let points = points!(self, self.x.len(), 2, 1);
            self.primitives.push_line(points);
        }
    }
}

impl Default for PathData {
    fn default() -> Self {
        Self {
            x: vec![0.0],
            y: vec![0.0],
            weight: vec![1.0],
            commands: vec![PathCommand::MoveTo],
            open_point_index: 0,
            primitives: Primitives::default(),
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct Path {
    inner: Rc<RefCell<PathData>>,
}

impl Path {
    pub(crate) fn push_lines_to(
        &self,
        x: &mut Vec<f32>,
        y: &mut Vec<f32>,
        layer_id: u32,
        layer_ids: &mut Vec<Option<u32>>,
        buffers: &mut ScratchBuffers,
    ) {
        self.inner.borrow().primitives.push_lines_to(x, y, layer_id, layer_ids, buffers);
    }

    #[inline]
    pub fn transform(&self, transform: &[f32; 9]) -> Self {
        let inner = self.inner.borrow();
        let mut data = PathData {
            x: inner.x.clone(),
            y: inner.y.clone(),
            weight: inner.weight.clone(),
            commands: inner.commands.clone(),
            open_point_index: inner.open_point_index.clone(),
            primitives: Primitives::default(),
        };

        data.x
            .par_iter_mut()
            .with_min_len(MIN_LEN)
            .zip(
                data.y
                    .par_iter_mut()
                    .with_min_len(MIN_LEN)
                    .zip(data.weight.par_iter_mut().with_min_len(MIN_LEN)),
            )
            .for_each(|(x, (y, weight))| {
                let old_x = *x;
                let old_y = *y;
                let old_weight = *weight;

                *x = transform[0]
                    .mul_add(old_x, transform[1].mul_add(old_y, transform[2] * old_weight));
                *y = transform[3]
                    .mul_add(old_x, transform[4].mul_add(old_y, transform[5] * old_weight));
                *weight = transform[6]
                    .mul_add(old_x, transform[7].mul_add(old_y, transform[8] * old_weight));
            });

        let mut i = 0;
        for command in &data.commands {
            match command {
                PathCommand::MoveTo => {
                    i += 1;

                    data.primitives.push_contour(Contour);
                }
                PathCommand::LineTo => {
                    i += 1;

                    let points = points!(data, i, 2, 1);
                    data.primitives.push_line(points);
                }
                PathCommand::QuadTo => {
                    i += 2;

                    let points = points!(data, i, 3, 2, 1);
                    data.primitives.push_quad(points);
                }
                PathCommand::CubicTo => {
                    i += 3;

                    let points = points!(data, i, 4, 3, 2, 1);
                    data.primitives.push_cubic(points);
                }
            }
        }

        Self { inner: Rc::new(RefCell::new(data)) }
    }
}

impl Eq for Path {}

impl PartialEq for Path {
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.inner, &other.inner)
    }
}

#[derive(Clone, Debug, Default)]
pub struct PathBuilder {
    inner: Rc<RefCell<PathData>>,
}

impl PathBuilder {
    #[inline]
    pub fn new() -> Self {
        Self::default()
    }

    #[inline]
    pub fn move_to(&mut self, p: Point<f32>) -> &mut Self {
        {
            let mut inner = self.inner.borrow_mut();
            let len = inner.x.len();

            if matches!(inner.commands[inner.commands.len() - 1], PathCommand::MoveTo) {
                inner.x[len - 1] = p.x;
                inner.y[len - 1] = p.y;
                inner.weight[len - 1] = 1.0;
            } else {
                inner.close();

                let open_point_index = inner.x.len();

                inner.x.push(p.x);
                inner.y.push(p.y);
                inner.weight.push(1.0);

                inner.commands.push(PathCommand::MoveTo);

                inner.primitives.push_contour(Contour);

                inner.open_point_index = open_point_index;
            }
        }

        self
    }

    #[inline]
    pub fn line_to(&mut self, p: Point<f32>) -> &mut Self {
        {
            let mut inner = self.inner.borrow_mut();

            inner.x.push(p.x);
            inner.y.push(p.y);
            inner.weight.push(1.0);

            inner.commands.push(PathCommand::LineTo);

            let points = points!(inner, inner.x.len(), 2, 1);
            inner.primitives.push_line(points);
        }

        self
    }

    #[inline]
    pub fn quad_to(&mut self, p1: Point<f32>, p2: Point<f32>) -> &mut Self {
        {
            let mut inner = self.inner.borrow_mut();

            inner.x.push(p1.x);
            inner.y.push(p1.y);
            inner.weight.push(1.0);

            inner.x.push(p2.x);
            inner.y.push(p2.y);
            inner.weight.push(1.0);

            inner.commands.push(PathCommand::QuadTo);

            let points = points!(inner, inner.x.len(), 3, 2, 1);
            inner.primitives.push_quad(points);
        }

        self
    }

    #[inline]
    pub fn cubic_to(&mut self, p1: Point<f32>, p2: Point<f32>, p3: Point<f32>) -> &mut Self {
        {
            let mut inner = self.inner.borrow_mut();

            inner.x.push(p1.x);
            inner.y.push(p1.y);
            inner.weight.push(1.0);

            inner.x.push(p2.x);
            inner.y.push(p2.y);
            inner.weight.push(1.0);

            inner.x.push(p3.x);
            inner.y.push(p3.y);
            inner.weight.push(1.0);

            inner.commands.push(PathCommand::CubicTo);

            let points = points!(inner, inner.x.len(), 4, 3, 2, 1);
            inner.primitives.push_cubic(points);
        }

        self
    }

    #[inline]
    pub fn rat_quad_to(&mut self, p1: Point<f32>, p2: Point<f32>, weight: f32) -> &mut Self {
        {
            let mut inner = self.inner.borrow_mut();

            inner.x.push(p1.x);
            inner.y.push(p1.y);
            inner.weight.push(weight);

            inner.x.push(p2.x);
            inner.y.push(p2.y);
            inner.weight.push(1.0);

            inner.commands.push(PathCommand::QuadTo);
        }

        self
    }

    #[inline]
    pub fn rat_cubic_to(
        &mut self,
        p1: Point<f32>,
        p2: Point<f32>,
        p3: Point<f32>,
        w1: f32,
        w2: f32,
    ) -> &mut Self {
        {
            let mut inner = self.inner.borrow_mut();

            inner.x.push(p1.x);
            inner.y.push(p1.y);
            inner.weight.push(w1);

            inner.x.push(p2.x);
            inner.y.push(p2.y);
            inner.weight.push(w2);

            inner.x.push(p3.x);
            inner.y.push(p3.y);
            inner.weight.push(1.0);

            inner.commands.push(PathCommand::CubicTo);
        }

        self
    }

    #[inline]
    pub fn build(&mut self) -> Path {
        let mut inner = self.inner.borrow_mut();

        inner.close();

        Path { inner: Rc::clone(&self.inner) }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::Point;

    fn dist(p0: Point<f32>, p1: Point<f32>, p2: Point<f32>) -> f32 {
        let d10 = p1 - p0;
        let d21 = p2 - p1;

        (d21.x * d10.y - d10.x * d21.y).abs() / d21.len()
    }

    macro_rules! curve {
        (
            ( $p0x:expr , $p0y:expr ) ,
            ( $p1x:expr , $p1y:expr ) ,
            ( $p2x:expr , $p2y:expr ) ,
            ( $p3x:expr , $p3y:expr ) $( , )?
        ) => {
            [
                WeightedPoint { point: Point::new($p0x, $p0y), weight: 1.0 },
                WeightedPoint { point: Point::new($p1x, $p1y), weight: 1.0 },
                WeightedPoint { point: Point::new($p2x, $p2y), weight: 1.0 },
                WeightedPoint { point: Point::new($p3x, $p3y), weight: 1.0 },
            ]
        };
        (
            ( $p0x:expr , $p0y:expr ) ,
            ( $p1x:expr , $p1y:expr ) ,
            ( $p2x:expr , $p2y:expr ) $( , )?
        ) => {
            [
                WeightedPoint { point: Point::new($p0x, $p0y), weight: 1.0 },
                WeightedPoint { point: Point::new($p1x, $p1y), weight: 1.0 },
                WeightedPoint { point: Point::new($p2x, $p2y), weight: 1.0 },
            ]
        };
        ( ( $p0x:expr , $p0y:expr ) , ( $p1x:expr , $p1y:expr ) $( , )? ) => {
            [
                WeightedPoint { point: Point::new($p0x, $p0y), weight: 1.0 },
                WeightedPoint { point: Point::new($p1x, $p1y), weight: 1.0 },
            ]
        };
    }

    #[test]
    fn quads() {
        let mut primitives = Primitives::default();

        primitives.push_quad(curve![(2.0, 0.0), (0.0, 1.0), (10.0, 1.0)]);
        primitives.push_quad(curve![(10.0, 1.0), (20.0, 1.0), (18.0, 0.0)]);

        let mut x = Vec::new();
        let mut y = Vec::new();

        primitives.push_lines_to(
            &mut x,
            &mut y,
            0,
            &mut Vec::new(),
            &mut ScratchBuffers::default(),
        );

        assert_eq!(x.len(), 12);

        assert_eq!(x[0], 2.0);
        assert_eq!(y[0], 0.0);
        assert_eq!(x[11], 18.0);
        assert_eq!(y[11], 0.0);

        let a = Point::new(x[5], y[5]);
        let b = Point::new(x[6], y[6]);

        assert!((a - b).len() > 4.0);

        let points: Vec<_> = x.iter().zip(y.iter()).map(|(&x, &y)| Point::new(x, y)).collect();
        let max_dist = points
            .windows(3)
            .map(|window| dist(window[1], window[0], window[2]))
            .max_by(|a, b| a.partial_cmp(&b).unwrap())
            .unwrap();

        assert!(max_dist * max_dist < MAX_ERROR);
    }

    #[test]
    fn two_splines() {
        let mut primitives = Primitives::default();

        primitives.push_quad(curve![(0.0, 0.0), (1.0, 2.0), (2.0, 0.0)]);
        primitives.push_quad(curve![(3.0, 0.0), (4.0, 4.0), (5.0, 0.0)]);

        let mut x = Vec::new();
        let mut y = Vec::new();

        primitives.push_lines_to(
            &mut x,
            &mut y,
            0,
            &mut Vec::new(),
            &mut ScratchBuffers::default(),
        );

        assert_eq!(x.len(), 13);

        assert_eq!(x[0], 0.0);
        assert_eq!(y[0], 0.0);
        assert_eq!(x[5], 2.0);
        assert_eq!(y[5], 0.0);
        assert_eq!(x[6], 3.0);
        assert_eq!(y[6], 0.0);
        assert_eq!(x[12], 5.0);
        assert_eq!(y[12], 0.0);
    }

    #[test]
    fn collinear_quad() {
        let mut primitives = Primitives::default();

        primitives.push_quad(curve![(0.0, 0.0), (2.0, 0.0001), (1.0, 0.0)]);

        let mut x = Vec::new();
        let mut y = Vec::new();

        primitives.push_lines_to(
            &mut x,
            &mut y,
            0,
            &mut Vec::new(),
            &mut ScratchBuffers::default(),
        );

        assert_eq!(x.len(), 3);

        assert!((x[1] - 1.25).abs() < 0.01);
        assert!((y[1] - 0.0).abs() < 0.01);
    }

    #[test]
    fn overlapping_control_point_quad() {
        let mut primitives = Primitives::default();

        primitives.push_quad(curve![(0.0, 0.0), (0.0, 0.0), (1.0, 1.0)]);
        primitives.push_quad(curve![(1.0, 1.0), (1.0, 1.0), (1.0, 1.0)]);
        primitives.push_quad(curve![(1.0, 1.0), (2.0, 2.0), (2.0, 2.0)]);

        let mut x = Vec::new();
        let mut y = Vec::new();

        primitives.push_lines_to(
            &mut x,
            &mut y,
            0,
            &mut Vec::new(),
            &mut ScratchBuffers::default(),
        );

        assert_eq!(x.len(), 2);

        assert!((x[0] - 0.0).abs() < 0.01);
        assert!((y[0] - 0.0).abs() < 0.01);
        assert!((x[1] - 2.0).abs() < 0.01);
        assert!((y[1] - 2.0).abs() < 0.01);
    }

    #[test]
    fn rat_quad() {
        let mut primitives = Primitives::default();

        primitives.push_quad([
            WeightedPoint { point: Point::new(0.0, 0.0), weight: 1.0 },
            WeightedPoint { point: Point::new(1.0, 2.0), weight: 10.0 },
            WeightedPoint { point: Point::new(2.0, 0.0), weight: 1.0 },
        ]);

        let mut x = Vec::new();
        let mut y = Vec::new();

        primitives.push_lines_to(
            &mut x,
            &mut y,
            0,
            &mut Vec::new(),
            &mut ScratchBuffers::default(),
        );

        assert_eq!(x.len(), 6);

        let points: Vec<_> = x.iter().zip(y.iter()).map(|(&x, &y)| Point::new(x, y)).collect();

        let distances: Vec<_> =
            points.windows(2).map(|window| (window[1] - window[0]).len()).collect();

        assert!(distances[0] > 1.5);
        assert!(distances[1] < 0.2);
        assert!(distances[2] < 0.2);
        assert!(distances[3] < 0.2);
        assert!(distances[4] > 1.5);
    }

    #[test]
    fn lines_and_quads() {
        let mut primitives = Primitives::default();

        primitives.push_line(curve![(-1.0, -2.0), (0.0, 0.0)]);
        primitives.push_quad(curve![(0.0, 0.0), (1.0, 2.0), (2.0, 0.0)]);
        primitives.push_line(curve![(2.0, 0.0), (3.0, -2.0)]);
        primitives.push_line(curve![(3.0, -2.0), (4.0, 2.0)]);
        primitives.push_line(curve![(4.0, 2.0), (5.0, -4.0)]);
        primitives.push_line(curve![(5.0, -4.0), (6.0, 0.0)]);
        primitives.push_quad(curve![(6.0, 0.0), (7.0, 4.0), (8.0, 0.0)]);
        primitives.push_line(curve![(8.0, 0.0), (9.0, -4.0)]);

        let mut x = Vec::new();
        let mut y = Vec::new();

        primitives.push_lines_to(
            &mut x,
            &mut y,
            0,
            &mut Vec::new(),
            &mut ScratchBuffers::default(),
        );

        assert_eq!(x.len(), 14);

        assert_eq!(x[0], -1.0);
        assert_eq!(y[0], -2.0);
        assert_eq!(x[5], 3.0);
        assert_eq!(y[5], -2.0);
        assert_eq!(x[6], 4.0);
        assert_eq!(y[6], 2.0);
        assert_eq!(x[7], 5.0);
        assert_eq!(y[7], -4.0);
        assert_eq!(x[13], 9.0);
        assert_eq!(y[13], -4.0);
    }

    #[test]
    fn cubic() {
        let mut primitives = Primitives::default();

        primitives.push_cubic(curve![(0.0, 0.0), (5.0, 3.0), (-1.0, 3.0), (4.0, 0.0)]);

        let mut x = Vec::new();
        let mut y = Vec::new();

        primitives.push_lines_to(
            &mut x,
            &mut y,
            0,
            &mut Vec::new(),
            &mut ScratchBuffers::default(),
        );

        assert_eq!(x.len(), 10);

        assert!(x[2] > x[7]);
        assert!(x[3] > x[6]);
        assert!(x[4] > x[5]);

        assert!(y[0] < y[1]);
        assert!(y[1] < y[2]);
        assert!(y[2] < y[3]);
        assert!(y[3] < y[4]);

        assert!(y[5] > y[6]);
        assert!(y[6] > y[7]);
        assert!(y[7] > y[8]);
        assert!(y[8] > y[9]);
    }

    #[test]
    fn rat_cubic_high() {
        let mut primitives = Primitives::default();

        primitives.push_cubic([
            WeightedPoint { point: Point::new(0.0, 0.0), weight: 1.0 },
            WeightedPoint { point: Point::new(5.0, 3.0), weight: 10.0 },
            WeightedPoint { point: Point::new(-1.0, 3.0), weight: 10.0 },
            WeightedPoint { point: Point::new(4.0, 0.0), weight: 1.0 },
        ]);

        let mut x = Vec::new();
        let mut y = Vec::new();

        primitives.push_lines_to(
            &mut x,
            &mut y,
            0,
            &mut Vec::new(),
            &mut ScratchBuffers::default(),
        );

        assert_eq!(x.len(), 57);
    }

    #[test]
    fn rat_cubic_low() {
        let mut primitives = Primitives::default();

        primitives.push_cubic([
            WeightedPoint { point: Point::new(0.0, 0.0), weight: 1.0 },
            WeightedPoint { point: Point::new(5.0, 3.0), weight: 0.5 },
            WeightedPoint { point: Point::new(-1.0, 3.0), weight: 0.5 },
            WeightedPoint { point: Point::new(4.0, 0.0), weight: 1.0 },
        ]);

        let mut x = Vec::new();
        let mut y = Vec::new();

        primitives.push_lines_to(
            &mut x,
            &mut y,
            0,
            &mut Vec::new(),
            &mut ScratchBuffers::default(),
        );

        assert_eq!(x.len(), 8);
    }

    #[test]
    fn collinear_cubic() {
        let mut primitives = Primitives::default();

        primitives.push_cubic(curve![(1.0, 0.0), (0.0, 0.0), (3.0, 0.0), (2.0, 0.0)]);

        let mut x = Vec::new();
        let mut y = Vec::new();

        primitives.push_lines_to(
            &mut x,
            &mut y,
            0,
            &mut Vec::new(),
            &mut ScratchBuffers::default(),
        );

        assert_eq!(x.len(), 7);

        assert_eq!(x[0], 1.0);
        assert_eq!(y[0], 0.0);

        assert!(x[1] > 0.5);
        assert!(x[1] < 1.0);
        assert_eq!(y[1], 0.0);

        assert!(x[2] >= 1.0);
        assert!(x[2] < 2.0);
        assert_eq!(y[2], 0.0);

        assert!(x[3] > 1.0);
        assert!(x[3] < 2.0);
        assert_eq!(y[3], 0.0);

        assert!(x[4] > 1.0);
        assert!(x[4] <= 2.0);
        assert_eq!(y[4], 0.0);

        assert!(x[5] > 2.0);
        assert!(x[5] < 2.5);
        assert_eq!(y[5], 0.0);

        assert_eq!(x[6], 2.0);
        assert_eq!(y[6], 0.0);
    }

    #[test]
    fn overlapping_control_point_cubic_line() {
        let mut primitives = Primitives::default();

        primitives.push_cubic(curve![(0.0, 0.0), (0.0, 0.0), (1.0, 1.0), (1.0, 1.0)]);
        primitives.push_cubic(curve![(1.0, 1.0), (1.0, 1.0), (1.0, 1.0), (1.0, 1.0)]);
        primitives.push_cubic(curve![(1.0, 1.0), (1.0, 1.0), (2.0, 2.0), (2.0, 2.0)]);

        let mut x = Vec::new();
        let mut y = Vec::new();

        primitives.push_lines_to(
            &mut x,
            &mut y,
            0,
            &mut Vec::new(),
            &mut ScratchBuffers::default(),
        );

        assert_eq!(x.len(), 9);

        for x in x.windows(2) {
            assert!(x[0] < x[1]);
        }

        for y in y.windows(2) {
            assert!(y[0] < y[1]);
        }

        for (&x, &y) in x.iter().zip(y.iter()) {
            assert_eq!(x, y);
        }

        assert!((x[0] - 0.0).abs() < 0.01);
        assert!((y[0] - 0.0).abs() < 0.01);
        assert!((x[8] - 2.0).abs() < 0.01);
        assert!((y[8] - 2.0).abs() < 0.01);
    }

    #[test]
    fn ring() {
        let mut primitives = Primitives::default();

        primitives.push_cubic(curve![(0.0, 2.0), (2.0, 2.0), (2.0, 2.0), (2.0, 0.0)]);
        primitives.push_cubic(curve![(2.0, 0.0), (2.0, -2.0), (2.0, -2.0), (0.0, -2.0)]);
        primitives.push_cubic(curve![(0.0, -2.0), (-2.0, -2.0), (-2.0, -2.0), (-2.0, 0.0)]);
        primitives.push_cubic(curve![(-2.0, 0.0), (-2.0, 2.0), (-2.0, 2.0), (0.0, 2.0)]);

        primitives.push_contour(Contour);

        primitives.push_cubic(curve![(0.0, 1.0), (-1.0, 1.0), (-1.0, 1.0), (-1.0, 0.0)]);
        primitives.push_cubic(curve![(-1.0, 0.0), (-1.0, -1.0), (-1.0, -1.0), (0.0, -1.0)]);
        primitives.push_cubic(curve![(0.0, -1.0), (1.0, -1.0), (1.0, -1.0), (1.0, 0.0)]);
        primitives.push_cubic(curve![(1.0, 0.0), (1.0, 1.0), (1.0, 1.0), (0.0, 1.0)]);

        let mut layer_ids = Vec::new();

        primitives.push_lines_to(
            &mut Vec::new(),
            &mut Vec::new(),
            0,
            &mut layer_ids,
            &mut ScratchBuffers::default(),
        );

        assert_eq!(layer_ids.len(), 38);

        assert_eq!(layer_ids.iter().filter(|layer_id| layer_id.is_none()).count(), 2);

        assert_eq!(layer_ids[20], None);
        assert_eq!(layer_ids[37], None);
    }

    #[test]
    fn ring_overlapping_start() {
        let mut primitives = Primitives::default();

        primitives.push_cubic(curve![(0.0, 1.0), (-1.0, 1.0), (-1.0, 1.0), (-1.0, 0.0)]);
        primitives.push_cubic(curve![(-1.0, 0.0), (-1.0, -1.0), (-1.0, -1.0), (0.0, -1.0)]);
        primitives.push_cubic(curve![(0.0, -1.0), (1.0, -1.0), (1.0, -1.0), (1.0, 0.0)]);
        primitives.push_cubic(curve![(1.0, 0.0), (1.0, 1.0), (1.0, 1.0), (0.0, 1.0)]);

        primitives.push_contour(Contour);

        primitives.push_cubic(curve![(0.0, 1.0), (1.0, 1.0), (1.0, 1.0), (1.0, 2.0)]);
        primitives.push_cubic(curve![(1.0, 2.0), (1.0, 3.0), (1.0, 3.0), (0.0, 3.0)]);
        primitives.push_cubic(curve![(0.0, 3.0), (-1.0, 3.0), (-1.0, 3.0), (-1.0, 2.0)]);
        primitives.push_cubic(curve![(-1.0, 2.0), (-1.0, 1.0), (-1.0, 1.0), (0.0, 1.0)]);

        let mut layer_ids = Vec::new();

        primitives.push_lines_to(
            &mut Vec::new(),
            &mut Vec::new(),
            0,
            &mut layer_ids,
            &mut ScratchBuffers::default(),
        );

        assert_eq!(layer_ids.len(), 34);

        assert_eq!(layer_ids.iter().filter(|layer_id| layer_id.is_none()).count(), 2);

        assert_eq!(layer_ids[16], None);
        assert_eq!(layer_ids[33], None);
    }
}
