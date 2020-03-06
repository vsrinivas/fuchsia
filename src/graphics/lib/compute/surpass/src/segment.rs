// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use rayon::prelude::*;

use crate::{
    extend::ExtendTuple10, layer::Layer, point::Point, PIXEL_MASK, PIXEL_SHIFT, PIXEL_WIDTH,
};

const MIN_LEN: usize = 1_024;

#[inline(always)]
pub(crate) fn to_sub_pixel(v: f32) -> i32 {
    (v.mul_add(PIXEL_WIDTH as f32, 0.5f32.copysign(v))) as i32
}

#[derive(Clone, Copy, Debug)]
pub struct Segment<T> {
    pub p0: Point<T>,
    pub p1: Point<T>,
}

impl<T> Segment<T> {
    pub fn new(p0: Point<T>, p1: Point<T>) -> Self {
        Self { p0, p1 }
    }
}

#[derive(Debug, Default)]
pub struct LinesBuilder {
    lines: Lines,
}

impl LinesBuilder {
    #[inline]
    pub fn new() -> Self {
        Self::default()
    }

    #[inline]
    pub fn len(&self) -> usize {
        self.lines.p0xs.len()
    }

    #[inline]
    pub fn push(&mut self, layer_id: u16, segment: &Segment<f32>) {
        self.lines.p0xs.push(segment.p0.x);
        self.lines.p0ys.push(segment.p0.y);
        self.lines.p1xs.push(segment.p1.x);
        self.lines.p1ys.push(segment.p1.y);
        self.lines.layer_ids.push(layer_id);
    }

    pub fn retain<F>(&mut self, mut f: F)
    where
        F: FnMut(u16) -> bool,
    {
        let len = self.len();
        let mut del = 0;

        for i in 0..len {
            if !f(self.lines.layer_ids[i]) {
                del += 1;
                continue;
            }

            if del > 0 {
                self.lines.p0xs.swap(i - del, i);
                self.lines.p0ys.swap(i - del, i);
                self.lines.p1xs.swap(i - del, i);
                self.lines.p1ys.swap(i - del, i);
                self.lines.layer_ids.swap(i - del, i);
            }
        }

        if del > 0 {
            self.lines.p0xs.truncate(len - del);
            self.lines.p0ys.truncate(len - del);
            self.lines.p1xs.truncate(len - del);
            self.lines.p1ys.truncate(len - del);
            self.lines.layer_ids.truncate(len - del);
        }
    }

    #[inline]
    pub fn set_default_transform(&mut self, affine_transform: &[f32; 6]) {
        self.lines.transform = Some(*affine_transform);
    }

    pub fn build<F>(mut self, layers: F) -> Lines
    where
        F: Fn(u16) -> Option<Layer> + Send + Sync,
    {
        let transform = self.lines.transform;
        let ps_layers = self.lines.p0xs.par_iter().with_min_len(MIN_LEN).zip_eq(
            self.lines.p0ys.par_iter().with_min_len(MIN_LEN).zip_eq(
                self.lines.p1xs.par_iter().with_min_len(MIN_LEN).zip_eq(
                    self.lines
                        .p1ys
                        .par_iter()
                        .with_min_len(MIN_LEN)
                        .zip_eq(self.lines.layer_ids.par_iter().with_min_len(MIN_LEN)),
                ),
            ),
        );
        let par_iter = ps_layers.map(|(&p0x, (&p0y, (&p1x, (&p1y, &layer_id))))| {
            let layer = layers(layer_id);

            if let Some(Layer { is_enabled: false, .. }) = layer {
                return Default::default();
            }

            let order = layer.as_ref().map(|layer| layer.order).flatten().unwrap_or(layer_id);

            fn transform_point(point: (f32, f32), transform: &[f32; 6]) -> (f32, f32) {
                (
                    transform[0].mul_add(point.0, transform[1].mul_add(point.1, transform[4])),
                    transform[2].mul_add(point.0, transform[3].mul_add(point.1, transform[5])),
                )
            }

            let transform = layer
                .as_ref()
                .map(|layer| layer.affine_transform.as_ref())
                .flatten()
                .or_else(|| transform.as_ref());
            let (p0x, p0y, p1x, p1y) = if let Some(transform) = transform {
                let (p0x, p0y) = transform_point((p0x, p0y), transform);
                let (p1x, p1y) = transform_point((p1x, p1y), transform);

                (p0x, p0y, p1x, p1y)
            } else {
                (p0x, p0y, p1x, p1y)
            };

            let p0x_sub = to_sub_pixel(p0x);
            let p0y_sub = to_sub_pixel(p0y);
            let p1x_sub = to_sub_pixel(p1x);
            let p1y_sub = to_sub_pixel(p1y);

            if p0y_sub == p1y_sub {
                return Default::default();
            }

            let dx = p1x_sub - p0x_sub;
            let dy = p1y_sub - p0y_sub;

            let dx_dy = dx.abs() > dy.abs();
            let sum_0 = dx + dy > 0 || dx == -dy && dx < dy;

            let (start_sub, end_sub) = match (sum_0, dx_dy) {
                (true, true) => (p0x_sub, p1x_sub),
                (true, false) => (p0y_sub, p1y_sub),
                (false, true) => (p1x_sub, p0x_sub),
                (false, false) => (p1y_sub, p0y_sub),
            };

            let start = start_sub >> PIXEL_SHIFT as i32;
            let end = if end_sub & PIXEL_MASK as i32 == 0 {
                end_sub >> PIXEL_SHIFT as i32
            } else {
                (end_sub >> PIXEL_SHIFT as i32) + 1
            };

            let length = if p0y_sub != p1y_sub { (end - start) as usize } else { 0 };

            let start_f32 = start_sub as f32 * (PIXEL_WIDTH as f32).recip();
            let end_f32 = end_sub as f32 * (PIXEL_WIDTH as f32).recip();

            let p0x = p0x_sub as f32 * (PIXEL_WIDTH as f32).recip();
            let p0y = p0y_sub as f32 * (PIXEL_WIDTH as f32).recip();

            let (px, py) = if dx_dy { (p0x, p0y) } else { (p0y, p0x) };

            let slope = if dx_dy { dy as f32 / dx as f32 } else { dx as f32 / dy as f32 };
            let slope_recip = slope.recip();

            let py_slope_px = slope.mul_add(-px, py);
            let px_slope_recip_py = slope_recip.mul_add(-py, px);

            let octant = match (dx_dy, dx > 0, dy > 0) {
                (true, true, true) => 0,
                (false, true, true) => 1,
                (false, false, true) => 2,
                (true, false, true) => 3,
                (true, false, false) => 4,
                (false, false, false) => 5,
                (false, true, false) => 6,
                (true, true, false) => 7,
            };

            (
                order,
                start,
                start_f32,
                end_f32,
                length,
                slope,
                slope_recip,
                py_slope_px,
                px_slope_recip_py,
                octant,
            )
        });

        ExtendTuple10::new((
            &mut self.lines.orders,
            &mut self.lines.starts,
            &mut self.lines.starts_f32,
            &mut self.lines.ends_f32,
            &mut self.lines.lengths,
            &mut self.lines.slopes,
            &mut self.lines.slopes_recip,
            &mut self.lines.py_slope_pxs,
            &mut self.lines.px_slope_recip_pys,
            &mut self.lines.octants,
        ))
        .par_extend(par_iter);

        self.lines
    }
}

#[derive(Debug, Default)]
pub struct Lines {
    pub p0xs: Vec<f32>,
    pub p0ys: Vec<f32>,
    pub p1xs: Vec<f32>,
    pub p1ys: Vec<f32>,
    transform: Option<[f32; 6]>,
    pub layer_ids: Vec<u16>,
    pub orders: Vec<u16>,
    pub starts: Vec<i32>,
    pub starts_f32: Vec<f32>,
    pub ends_f32: Vec<f32>,
    pub lengths: Vec<usize>,
    pub slopes: Vec<f32>,
    pub slopes_recip: Vec<f32>,
    pub py_slope_pxs: Vec<f32>,
    pub px_slope_recip_pys: Vec<f32>,
    pub octants: Vec<u8>,
}

impl Lines {
    #[inline]
    pub fn len(&self) -> usize {
        self.p0xs.len()
    }

    #[inline]
    pub fn unwrap(mut self) -> LinesBuilder {
        self.orders.clear();
        self.starts.clear();
        self.starts_f32.clear();
        self.ends_f32.clear();
        self.lengths.clear();
        self.slopes.clear();
        self.slopes_recip.clear();
        self.py_slope_pxs.clear();
        self.px_slope_recip_pys.clear();
        self.octants.clear();

        LinesBuilder { lines: self }
    }

    #[inline]
    pub fn cleared(mut self) -> LinesBuilder {
        self.p0xs.clear();
        self.p0ys.clear();
        self.p1xs.clear();
        self.p1ys.clear();
        self.layer_ids.clear();
        self.orders.clear();
        self.starts.clear();
        self.starts_f32.clear();
        self.ends_f32.clear();
        self.lengths.clear();
        self.slopes.clear();
        self.slopes_recip.clear();
        self.py_slope_pxs.clear();
        self.px_slope_recip_pys.clear();
        self.octants.clear();

        LinesBuilder { lines: self }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const START_X: f32 = 2.0;
    const START_Y: f32 = 3.0;

    macro_rules! assert_approx {
        ( $left:expr, $right:expr ) => {{
            assert!(
                ($left - $right).abs() <= ::std::f32::EPSILON,
                format!("{:?} != {:?}", $left, $right),
            );
        }};
    }

    #[test]
    fn line_octant_1() {
        let mut builder = LinesBuilder::new();
        builder.push(
            0,
            &Segment::new(Point::new(START_X, START_Y), Point::new(START_X + 3.0, START_Y + 2.0)),
        );
        let lines = builder.build(|_| None);

        assert_eq!(lines.starts[0], START_X as i32);
        assert_eq!(lines.lengths[0], 3);
        assert_approx!(lines.slopes[0], 2.0 / 3.0);
        assert_approx!(lines.slopes_recip[0], 3.0 / 2.0);
        assert_eq!(lines.octants[0], 0);

        let y = lines.slopes[0] * (START_X + 1.5) + lines.py_slope_pxs[0];
        assert_approx!(y, START_Y + 1.0);
        let x = lines.slopes_recip[0] * (START_Y + 1.0) + lines.px_slope_recip_pys[0];
        assert_approx!(x, START_X + 1.5);
    }

    #[test]
    fn line_octant_2() {
        let mut builder = LinesBuilder::new();
        builder.push(
            0,
            &Segment::new(Point::new(START_X, START_Y), Point::new(START_X + 2.0, START_Y + 3.0)),
        );
        let lines = builder.build(|_| None);

        assert_eq!(lines.starts[0], START_Y as i32);
        assert_eq!(lines.lengths[0], 3);
        assert_approx!(lines.slopes[0], 2.0 / 3.0);
        assert_approx!(lines.slopes_recip[0], 3.0 / 2.0);
        assert_eq!(lines.octants[0], 1);

        let x = lines.slopes[0] * (START_Y + 1.5) + lines.py_slope_pxs[0];
        assert_approx!(x, START_X + 1.0);
        let y = lines.slopes_recip[0] * (START_X + 1.0) + lines.px_slope_recip_pys[0];
        assert_approx!(y, START_Y + 1.5);
    }

    #[test]
    fn line_octant_3() {
        let mut builder = LinesBuilder::new();
        builder.push(
            0,
            &Segment::new(Point::new(START_X, START_Y), Point::new(START_X - 2.0, START_Y + 3.0)),
        );
        let lines = builder.build(|_| None);

        assert_eq!(lines.starts[0], START_Y as i32);
        assert_eq!(lines.lengths[0], 3);
        assert_approx!(lines.slopes[0], -2.0 / 3.0);
        assert_approx!(lines.slopes_recip[0], -3.0 / 2.0);
        assert_eq!(lines.octants[0], 2);

        let x = lines.slopes[0] * (START_Y + 1.5) + lines.py_slope_pxs[0];
        assert_approx!(x, START_X - 1.0);
        let y = lines.slopes_recip[0] * (START_X - 1.0) + lines.px_slope_recip_pys[0];
        assert_approx!(y, START_Y + 1.5);
    }

    #[test]
    fn line_octant_4() {
        let mut builder = LinesBuilder::new();
        builder.push(
            0,
            &Segment::new(Point::new(START_X, START_Y), Point::new(START_X - 3.0, START_Y + 2.0)),
        );
        let lines = builder.build(|_| None);

        assert_eq!(lines.starts[0], (START_X - 3.0) as i32);
        assert_eq!(lines.lengths[0], 3);
        assert_approx!(lines.slopes[0], -2.0 / 3.0);
        assert_approx!(lines.slopes_recip[0], -3.0 / 2.0);
        assert_eq!(lines.octants[0], 3);

        let y = lines.slopes[0] * (START_X - 1.5) + lines.py_slope_pxs[0];
        assert_approx!(y, START_Y + 1.0);
        let x = lines.slopes_recip[0] * (START_Y + 1.0) + lines.px_slope_recip_pys[0];
        assert_approx!(x, START_X - 1.5);
    }

    #[test]
    fn line_octant_5() {
        let mut builder = LinesBuilder::new();
        builder.push(
            0,
            &Segment::new(Point::new(START_X, START_Y), Point::new(START_X - 3.0, START_Y - 2.0)),
        );
        let lines = builder.build(|_| None);

        assert_eq!(lines.starts[0], (START_X - 3.0) as i32);
        assert_eq!(lines.lengths[0], 3);
        assert_approx!(lines.slopes[0], 2.0 / 3.0);
        assert_approx!(lines.slopes_recip[0], 3.0 / 2.0);
        assert_eq!(lines.octants[0], 4);

        let y = lines.slopes[0] * (START_X - 1.5) + lines.py_slope_pxs[0];
        assert_approx!(y, START_Y - 1.0);
        let x = lines.slopes_recip[0] * (START_Y - 1.0) + lines.px_slope_recip_pys[0];
        assert_approx!(x, START_X - 1.5);
    }

    #[test]
    fn line_octant_6() {
        let mut builder = LinesBuilder::new();
        builder.push(
            0,
            &Segment::new(Point::new(START_X, START_Y), Point::new(START_X - 2.0, START_Y - 3.0)),
        );
        let lines = builder.build(|_| None);

        assert_eq!(lines.starts[0], (START_Y - 3.0) as i32);
        assert_eq!(lines.lengths[0], 3);
        assert_approx!(lines.slopes[0], 2.0 / 3.0);
        assert_approx!(lines.slopes_recip[0], 3.0 / 2.0);
        assert_eq!(lines.octants[0], 5);

        let x = lines.slopes[0] * (START_Y - 1.5) + lines.py_slope_pxs[0];
        assert_approx!(x, START_X - 1.0);
        let y = lines.slopes_recip[0] * (START_X - 1.0) + lines.px_slope_recip_pys[0];
        assert_approx!(y, START_Y - 1.5);
    }

    #[test]
    fn line_octant_7() {
        let mut builder = LinesBuilder::new();
        builder.push(
            0,
            &Segment::new(Point::new(START_X, START_Y), Point::new(START_X + 2.0, START_Y - 3.0)),
        );
        let lines = builder.build(|_| None);

        assert_eq!(lines.starts[0], (START_Y - 3.0) as i32);
        assert_eq!(lines.lengths[0], 3);
        assert_approx!(lines.slopes[0], -2.0 / 3.0);
        assert_approx!(lines.slopes_recip[0], -3.0 / 2.0);
        assert_eq!(lines.octants[0], 6);

        let x = lines.slopes[0] * (START_Y - 1.5) + lines.py_slope_pxs[0];
        assert_approx!(x, START_X + 1.0);
        let y = lines.slopes_recip[0] * (START_X + 1.0) + lines.px_slope_recip_pys[0];
        assert_approx!(y, START_Y - 1.5);
    }

    #[test]
    fn line_octant_8() {
        let mut builder = LinesBuilder::new();
        builder.push(
            0,
            &Segment::new(Point::new(START_X, START_Y), Point::new(START_X + 3.0, START_Y - 2.0)),
        );
        let lines = builder.build(|_| None);

        assert_eq!(lines.starts[0], START_X as i32);
        assert_eq!(lines.lengths[0], 3);
        assert_approx!(lines.slopes[0], -2.0 / 3.0);
        assert_approx!(lines.slopes_recip[0], -3.0 / 2.0);
        assert_eq!(lines.octants[0], 7);

        let y = lines.slopes[0] * (START_X + 1.5) + lines.py_slope_pxs[0];
        assert_approx!(y, START_Y - 1.0);
        let x = lines.slopes_recip[0] * (START_Y - 1.0) + lines.px_slope_recip_pys[0];
        assert_approx!(x, START_X + 1.5);
    }

    #[test]
    fn octant_borders() {
        let mut builder = LinesBuilder::new();

        for &x in &[-10.0, 0.0, 10.0] {
            for &y in &[-10.0, 0.0, 10.0] {
                if x != 0.0 && y != 0.0 {
                    builder.push(0, &Segment::new(Point::new(0.0, 0.0), Point::new(x, y)));
                }
            }
        }

        let lines = builder.build(|_| None);

        assert!(lines.lengths.iter().all(|&len| len == 10));
    }

    #[test]
    fn empty_segment() {
        let mut builder = LinesBuilder::new();
        builder.push(0, &Segment::new(Point::new(2.0, 2.0), Point::new(2.0, 2.0)));
        let lines = builder.build(|_| None);

        assert_eq!(lines.lengths, &[0]);
    }
}
