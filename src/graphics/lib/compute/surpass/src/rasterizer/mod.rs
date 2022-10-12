// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use rayon::prelude::*;

use crate::{Lines, PIXEL_SHIFT, PIXEL_WIDTH};

mod grouped_iter;
mod pixel_segment;

use grouped_iter::GroupedIter;
pub use pixel_segment::{bit_field_lens, search_last_by_key, PixelSegment, PixelSegmentUnpacked};

// This finds the ith term in the ordered union of two sequences:
//
// 1. a * x + c
// 2. b * x + d
//
// It works by estimating the amount of items that came from sequence 1 and
// sequence 2 such that the next item will be the ith. This results in two
// indices from each sequence. The final step is to simply pick the smaller one
// which naturally comes next.
fn find(i: i32, sum_recip: f32, cd: f32, a: f32, b: f32, c: f32, d: f32) -> f32 {
    const BIAS: f32 = -0.000_000_5;

    let i = i as f32;

    let ja = if b.is_finite() { (b.mul_add(i, -cd).mul_add(sum_recip, BIAS)).ceil() } else { i };
    let jb = if a.is_finite() { (a.mul_add(i, cd).mul_add(sum_recip, BIAS)).ceil() } else { i };

    let guess_a = a.mul_add(ja, c);
    let guess_b = b.mul_add(jb, d);

    guess_a.min(guess_b)
}

fn round(v: f32) -> i32 {
    unsafe { (v + 0.5).floor().to_int_unchecked() }
}

#[derive(Debug, Default)]
pub struct Rasterizer<const TW: usize, const TH: usize> {
    segments: Vec<PixelSegment<TW, TH>>,
}

impl<const TW: usize, const TH: usize> Rasterizer<TW, TH> {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn segments(&self) -> &[PixelSegment<TW, TH>] {
        self.segments.as_slice()
    }

    #[inline(never)]
    pub fn rasterize(&mut self, lines: &Lines) {
        // Shard the workload into set of similar output size in PixelSegment.
        let iter = GroupedIter::new(&lines.lengths);

        iter.into_par_iter()
            .with_min_len(256)
            .map(|(li, pi)| {
                let a = lines.a[li as usize];
                let b = lines.b[li as usize];
                let c = lines.c[li as usize];
                let d = lines.d[li as usize];

                let i = pi as i32 - i32::from(c != 0.0) - i32::from(d != 0.0);

                let i0 = i;
                let i1 = i + 1;

                let sum_recip = (a + b).recip();
                let cd = c - d;

                let t0 = find(i0, sum_recip, cd, a, b, c, d).max(0.0);
                let t1 = find(i1, sum_recip, cd, a, b, c, d).min(1.0);

                let x0f = t0.mul_add(lines.dx[li as usize], lines.x0[li as usize]);
                let y0f = t0.mul_add(lines.dy[li as usize], lines.y0[li as usize]);
                let x1f = t1.mul_add(lines.dx[li as usize], lines.x0[li as usize]);
                let y1f = t1.mul_add(lines.dy[li as usize], lines.y0[li as usize]);

                let x0_sub = round(x0f);
                let x1_sub = round(x1f);
                let y0_sub = round(y0f);
                let y1_sub = round(y1f);

                let border_x = x0_sub.min(x1_sub) >> PIXEL_SHIFT;
                let border_y = y0_sub.min(y1_sub) >> PIXEL_SHIFT;

                let tile_x = (border_x >> TW.trailing_zeros() as i32) as i16;
                let tile_y = (border_y >> TH.trailing_zeros() as i32) as i16;
                let local_x = (border_x & (TW - 1) as i32) as u8;
                let local_y = (border_y & (TH - 1) as i32) as u8;

                let border = (border_x << PIXEL_SHIFT) + PIXEL_WIDTH as i32;
                let height = y1_sub - y0_sub;

                let double_area_multiplier =
                    ((x1_sub - x0_sub).abs() + 2 * (border - x0_sub.max(x1_sub))) as u8;
                let cover = height as i8;

                PixelSegment::new(
                    lines.orders[li as usize],
                    tile_x,
                    tile_y,
                    local_x,
                    local_y,
                    double_area_multiplier,
                    cover,
                )
            })
            .collect_into_vec(&mut self.segments);
    }

    const BIT_FIELD_LENGTH: [usize; 7] = bit_field_lens::<TW, TH>();

    #[inline]
    pub fn sort(&mut self) {
        // Sort by (tile_y, tile_x, layer_id).
        let offset = Self::BIT_FIELD_LENGTH[3]
            + Self::BIT_FIELD_LENGTH[4]
            + Self::BIT_FIELD_LENGTH[5]
            + Self::BIT_FIELD_LENGTH[6];
        self.segments.par_sort_unstable_by_key(|segment| {
            let segment: u64 = segment.into();
            segment >> offset
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::{
        rasterizer::pixel_segment::PixelSegmentUnpacked, GeomId, Layer, LinesBuilder, Order, Point,
        TILE_HEIGHT, TILE_WIDTH,
    };

    fn segments(p0: Point, p1: Point) -> Vec<PixelSegment<TILE_WIDTH, TILE_HEIGHT>> {
        let mut builder = LinesBuilder::new();
        builder.push(GeomId::default(), [p0, p1]);
        let lines = builder
            .build(|_| Some(Layer { order: Some(Order::new(0).unwrap()), ..Default::default() }));

        let mut rasterizer = Rasterizer::default();
        rasterizer.rasterize(&lines);

        rasterizer.segments().to_vec()
    }

    fn areas_and_covers(segments: &[PixelSegment<TILE_WIDTH, TILE_HEIGHT>]) -> Vec<(i16, i8)> {
        segments
            .iter()
            .map(|&segment| {
                let segment: PixelSegmentUnpacked = segment.into();
                (segment.double_area, segment.cover)
            })
            .collect()
    }

    #[test]
    fn area_cover_octant_1() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(3.0, 2.0))),
            [(11 * 16, 11), (5 * 8 + 2 * (5 * 8), 5), (5 * 8, 5), (11 * 16, 11)],
        );
    }

    #[test]
    fn area_cover_octant_2() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(2.0, 3.0))),
            [(16 * 11 + 2 * (16 * 5), 16), (8 * 5, 8), (8 * 5 + 2 * (8 * 11), 8), (16 * 11, 16)],
        );
    }

    #[test]
    fn area_cover_octant_3() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-2.0, 3.0))),
            [(16 * 11, 16), (8 * 5 + 2 * (8 * 11), 8), (8 * 5, 8), (16 * 11 + 2 * (16 * 5), 16)],
        );
    }

    #[test]
    fn area_cover_octant_4() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-3.0, 2.0))),
            [(11 * 16, 11), (5 * 8, 5), (5 * 8 + 2 * (5 * 8), 5), (11 * 16, 11)],
        );
    }

    #[test]
    fn area_cover_octant_5() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-3.0, -2.0))),
            [(-(11 * 16), -11), (-(5 * 8), -5), (-(5 * 8 + 2 * (5 * 8)), -5), (-(11 * 16), -11)],
        );
    }

    #[test]
    fn area_cover_octant_6() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-2.0, -3.0))),
            [
                (-(16 * 11), -16),
                (-(8 * 5 + 2 * (8 * 11)), -8),
                (-(8 * 5), -8),
                (-(16 * 11 + 2 * (16 * 5)), -16),
            ],
        );
    }

    #[test]
    fn area_cover_octant_7() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(2.0, -3.0))),
            [
                (-(16 * 11 + 2 * (16 * 5)), -16),
                (-(8 * 5), -8),
                (-(8 * 5 + 2 * (8 * 11)), -8),
                (-(16 * 11), -16),
            ],
        );
    }

    #[test]
    fn area_cover_octant_8() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(3.0, -2.0))),
            [(-(11 * 16), -11), (-(5 * 8 + 2 * (5 * 8)), -5), (-(5 * 8), -5), (-(11 * 16), -11)],
        );
    }

    #[test]
    fn area_cover_axis_0() {
        assert_eq!(areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(1.0, 0.0))), []);
    }

    #[test]
    fn area_cover_axis_45() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(1.0, 1.0))),
            [(16 * 16, 16)],
        );
    }

    #[test]
    fn area_cover_axis_90() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(0.0, 1.0))),
            [(2 * 16 * 16, 16)],
        );
    }

    #[test]
    fn area_cover_axis_135() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-1.0, 1.0))),
            [(16 * 16, 16)],
        );
    }

    #[test]
    fn area_cover_axis_180() {
        assert_eq!(areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-1.0, 0.0))), []);
    }

    #[test]
    fn area_cover_axis_225() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-1.0, -1.0))),
            [(-(16 * 16), -16)],
        );
    }

    #[test]
    fn area_cover_axis_270() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(0.0, -1.0))),
            [(2 * -(16 * 16), -16)],
        );
    }

    #[test]
    fn area_cover_axis_315() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-1.0, -1.0))),
            [(-(16 * 16), -16)],
        );
    }

    fn tiles(segments: &[PixelSegment<TILE_WIDTH, TILE_HEIGHT>]) -> Vec<(i16, i16, u8, u8)> {
        segments
            .iter()
            .map(|&segment| {
                let segment: PixelSegmentUnpacked = segment.into();
                (segment.tile_x, segment.tile_y, segment.local_x, segment.local_y)
            })
            .collect()
    }

    #[test]
    fn tile_octant_1() {
        assert_eq!(
            tiles(&segments(
                Point::new(TILE_WIDTH as f32, TILE_HEIGHT as f32),
                Point::new(TILE_WIDTH as f32 + 3.0, TILE_HEIGHT as f32 + 2.0),
            )),
            [(1, 1, 0, 0), (1, 1, 1, 0), (1, 1, 1, 1), (1, 1, 2, 1)],
        );
    }

    #[test]
    fn tile_octant_2() {
        assert_eq!(
            tiles(&segments(
                Point::new(TILE_WIDTH as f32, TILE_HEIGHT as f32),
                Point::new(TILE_WIDTH as f32 + 2.0, TILE_HEIGHT as f32 + 3.0),
            )),
            [(1, 1, 0, 0), (1, 1, 0, 1), (1, 1, 1, 1), (1, 1, 1, 2)],
        );
    }

    #[test]
    fn tile_octant_3() {
        assert_eq!(
            tiles(&segments(
                Point::new(-(TILE_WIDTH as f32), TILE_HEIGHT as f32),
                Point::new(-(TILE_WIDTH as f32) - 2.0, TILE_HEIGHT as f32 + 3.0),
            )),
            [
                (-1, 1, TILE_WIDTH as u8 - 1, 0),
                (-1, 1, TILE_WIDTH as u8 - 1, 1),
                (-1, 1, TILE_WIDTH as u8 - 2, 1),
                (-1, 1, TILE_WIDTH as u8 - 2, 2),
            ],
        );
    }

    #[test]
    fn tile_octant_4() {
        assert_eq!(
            tiles(&segments(
                Point::new(-(TILE_WIDTH as f32), TILE_HEIGHT as f32),
                Point::new(-(TILE_WIDTH as f32) - 3.0, TILE_HEIGHT as f32 + 2.0),
            )),
            [
                (-1, 1, TILE_WIDTH as u8 - 1, 0),
                (-1, 1, TILE_WIDTH as u8 - 2, 0),
                (-1, 1, TILE_WIDTH as u8 - 2, 1),
                (-1, 1, TILE_WIDTH as u8 - 3, 1),
            ],
        );
    }

    #[test]
    fn tile_octant_5() {
        assert_eq!(
            tiles(&segments(
                Point::new(-(TILE_WIDTH as f32), -(TILE_HEIGHT as f32)),
                Point::new(-(TILE_WIDTH as f32) - 3.0, -(TILE_HEIGHT as f32) - 2.0),
            )),
            [
                (-1, -1, TILE_WIDTH as u8 - 1, TILE_HEIGHT as u8 - 1),
                (-1, -1, TILE_WIDTH as u8 - 2, TILE_HEIGHT as u8 - 1),
                (-1, -1, TILE_WIDTH as u8 - 2, TILE_HEIGHT as u8 - 2),
                (-1, -1, TILE_WIDTH as u8 - 3, TILE_HEIGHT as u8 - 2),
            ],
        );
    }

    #[test]
    fn tile_octant_6() {
        assert_eq!(
            tiles(&segments(
                Point::new(-(TILE_WIDTH as f32), -(TILE_HEIGHT as f32)),
                Point::new(-(TILE_WIDTH as f32) - 2.0, -(TILE_HEIGHT as f32) - 3.0),
            )),
            [
                (-1, -1, TILE_WIDTH as u8 - 1, TILE_HEIGHT as u8 - 1),
                (-1, -1, TILE_WIDTH as u8 - 1, TILE_HEIGHT as u8 - 2),
                (-1, -1, TILE_WIDTH as u8 - 2, TILE_HEIGHT as u8 - 2),
                (-1, -1, TILE_WIDTH as u8 - 2, TILE_HEIGHT as u8 - 3),
            ],
        );
    }

    #[test]
    fn tile_octant_7() {
        assert_eq!(
            tiles(&segments(
                Point::new(TILE_WIDTH as f32, -(TILE_HEIGHT as f32)),
                Point::new(TILE_WIDTH as f32 + 2.0, -(TILE_HEIGHT as f32) - 3.0),
            )),
            [
                (1, -1, 0, TILE_HEIGHT as u8 - 1),
                (1, -1, 0, TILE_HEIGHT as u8 - 2),
                (1, -1, 1, TILE_HEIGHT as u8 - 2),
                (1, -1, 1, TILE_HEIGHT as u8 - 3),
            ],
        );
    }

    #[test]
    fn tile_octant_8() {
        assert_eq!(
            tiles(&segments(
                Point::new(TILE_WIDTH as f32, -(TILE_HEIGHT as f32)),
                Point::new(TILE_WIDTH as f32 + 3.0, -(TILE_HEIGHT as f32) - 2.0),
            )),
            [
                (1, -1, 0, TILE_HEIGHT as u8 - 1),
                (1, -1, 1, TILE_HEIGHT as u8 - 1),
                (1, -1, 1, TILE_HEIGHT as u8 - 2),
                (1, -1, 2, TILE_HEIGHT as u8 - 2),
            ],
        );
    }

    #[test]
    fn start_and_end_not_on_pixel_border() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.5, 0.25), Point::new(4.0, 2.0)))[0],
            (4 * 8, 4),
        );

        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(3.5, 1.75)))[4],
            (4 * 8 + 2 * (4 * 8), 4),
        );
    }
}
