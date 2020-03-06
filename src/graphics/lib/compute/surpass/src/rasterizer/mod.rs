// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::mem::MaybeUninit;

use rayon::prelude::*;

use crate::{
    segment::{to_sub_pixel, Lines, LinesBuilder},
    uninitialized::{write, UninitializedVec},
    PIXEL_MASK, PIXEL_SHIFT, PIXEL_WIDTH, TILE_MASK, TILE_SHIFT,
};

mod raster_segment;

pub use raster_segment::{search_last_by_key, CompactSegment};

const INDICES_MAX_CHUNK_SIZE: u32 = 16_258;
const SEGMENTS_MIN_LEN: usize = 4_096;

pub fn line_indices(
    builder: LinesBuilder,
    line_indices: &mut Vec<MaybeUninit<usize>>,
    pixel_indices: &mut Vec<MaybeUninit<i32>>,
) -> LinesBuilder {
    unsafe fn reserve_if_required<T>(vec: &mut Vec<T>, len: usize) {
        if vec.capacity() < len {
            vec.reserve(len - vec.capacity());
        }
        if vec.len() < len {
            vec.set_len(len);
        }
    }

    let lines = builder.build(|_| None);
    let len = lines.lengths.iter().copied().sum::<usize>();
    unsafe {
        reserve_if_required(line_indices, len);
        reserve_if_required(pixel_indices, len);
    }

    populate_indices(&lines.lengths, line_indices, pixel_indices);

    lines.unwrap()
}

#[derive(Debug)]
struct Chunk<'l, 'i> {
    start_index: usize,
    lengths: &'l [usize],
    lines_indices: &'i mut [MaybeUninit<usize>],
    pixel_indices: &'i mut [MaybeUninit<i32>],
}

fn chunks<'l, 'i>(
    mut lengths: &'l [usize],
    mut line_indices: &'i mut [MaybeUninit<usize>],
    mut pixel_indices: &'i mut [MaybeUninit<i32>],
    max_chunk_size: usize,
) -> Vec<Chunk<'l, 'i>> {
    let mut chunks = vec![];

    let mut lengths_index = 0;
    let mut indices_index = 0;
    let mut start_index = 0;

    for &len in lengths {
        lengths_index += 1;
        indices_index += len;

        if indices_index > max_chunk_size || lengths_index == lengths.len() {
            let (current_lengths, next_lengths) = lengths.split_at(lengths_index);
            let (current_line_indices, next_line_indices) =
                line_indices.split_at_mut(indices_index);
            let (current_pixel_indices, next_pixel_indices) =
                pixel_indices.split_at_mut(indices_index);

            chunks.push(Chunk {
                start_index,
                lengths: current_lengths,
                lines_indices: current_line_indices,
                pixel_indices: current_pixel_indices,
            });

            lengths = next_lengths;
            line_indices = next_line_indices;
            pixel_indices = next_pixel_indices;

            start_index += lengths_index;

            lengths_index = 0;
            indices_index = 0;
        }
    }

    chunks
}

fn populate_indices(
    lengths: &[usize],
    line_indices: &mut [MaybeUninit<usize>],
    pixel_indices: &mut [MaybeUninit<i32>],
) {
    let mut splits = chunks(lengths, line_indices, pixel_indices, INDICES_MAX_CHUNK_SIZE as usize);

    splits.par_iter_mut().for_each(|chunk| {
        let mut indices_index = 0;
        for (lengths_index, &len) in chunk.lengths.iter().enumerate() {
            let index = lengths_index + chunk.start_index;

            for i in indices_index..indices_index + len {
                write(&mut chunk.lines_indices[i], index);
                write(&mut chunk.pixel_indices[i], (i - indices_index) as i32);
            }

            indices_index += len;
        }
    });
}

#[inline]
fn intersects_y_border(y0_sub: i32, y1_sub: i32) -> bool {
    y0_sub & PIXEL_MASK as i32 != 0
        && y1_sub & PIXEL_MASK as i32 != 0
        && y0_sub >> PIXEL_SHIFT as i32 != y1_sub >> PIXEL_SHIFT as i32
}

fn tiles(border_x: i32, border_y: i32, octant: u8) -> (i16, i16, u8, u8) {
    let (border_x, border_y) = match octant {
        0 | 3 | 4 | 7 => (border_x, border_y),
        1 | 2 | 5 | 6 => (border_y, border_x),
        _ => unreachable!(),
    };

    let tile_i = (border_x >> TILE_SHIFT as i32) as i16;
    let tile_j = (border_y >> TILE_SHIFT as i32) as i16;
    let tile_x = (border_x & TILE_MASK as i32) as u8;
    let tile_y = (border_y & TILE_MASK as i32) as u8;

    (tile_i, tile_j, tile_x, tile_y)
}

fn area_cover(x0: i32, x1: i32, y0: i32, y1: i32, octant: u8) -> (i16, i8) {
    let (x0, x1, y0, y1) = match octant {
        0 => (x0, x1, y0, y1),
        1 => (y0, y1, x0, x1),
        2 => (y1, y0, x0, x1),
        3 => (x0, x1, y1, y0),
        4 => (x0, x1, y1, y0),
        5 => (y0, y1, x1, x0),
        6 => (y1, y0, x1, x0),
        7 => (x0, x1, y0, y1),
        _ => unreachable!(),
    };

    let border = (x0 & !(PIXEL_MASK as i32)) + PIXEL_WIDTH as i32;
    let height = y1 - y0;

    let triangle = ((y1 - y0) * (x1 - x0) / 2) as i16;
    let rectangle = (height * (border - x1)) as i16;

    let area = (triangle + rectangle) as i16;
    let cover = height as i8;

    (area, cover)
}

#[inline]
fn segment(
    layer: u16,
    x0: i32,
    x1: i32,
    y0: i32,
    y1: i32,
    border_x: i32,
    border_y: i32,
    octant: u8,
) -> CompactSegment {
    let (ti, tj, tx, ty) = tiles(border_x, border_y, octant);
    let (area, cover) = area_cover(x0, x1, y0, y1, octant);

    CompactSegment::new(0, tj, ti, layer, ty, tx, area, cover)
}

#[derive(Debug, Default)]
pub struct Rasterizer {
    line_indices: Vec<MaybeUninit<usize>>,
    pixel_indices: Vec<MaybeUninit<i32>>,
    segments: Vec<[CompactSegment; 2]>,
}

impl Rasterizer {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn segments(&self) -> &[CompactSegment] {
        unsafe {
            std::slice::from_raw_parts(self.segments.as_ptr() as *const _, self.segments.len() * 2)
        }
    }

    fn segments_mut(&mut self) -> &mut [CompactSegment] {
        unsafe {
            std::slice::from_raw_parts_mut(
                self.segments.as_mut_ptr() as *mut _,
                self.segments.len() * 2,
            )
        }
    }

    pub fn rasterize(&mut self, lines: &Lines) {
        let len = lines.lengths.iter().copied().sum::<usize>();
        self.line_indices.resize_uninit(len);
        self.pixel_indices.resize_uninit(len);
        self.segments.clear();

        populate_indices(&lines.lengths, &mut self.line_indices, &mut self.pixel_indices);

        let line_indices = unsafe { self.line_indices.assume_init() };
        let pixel_indices = unsafe { self.pixel_indices.assume_init() };

        let par_iter = line_indices
            .par_iter()
            .with_min_len(SEGMENTS_MIN_LEN)
            .zip_eq(pixel_indices.par_iter().with_min_len(SEGMENTS_MIN_LEN))
            .map(|(&li, &pi)| {
                let layer = lines.orders[li];
                let py_slope_px = lines.py_slope_pxs[li];
                let px_slope_recip_py = lines.px_slope_recip_pys[li];
                let octant = lines.octants[li];

                let x0 = lines.starts[li] + pi;
                let x1 = x0 + 1;

                let border_x = x0;

                let x0 = (x0 as f32).max(lines.starts_f32[li]);
                let x1 = (x1 as f32).min(lines.ends_f32[li]);

                let y0 = lines.slopes[li].mul_add(x0, py_slope_px);
                let y1 = lines.slopes[li].mul_add(x1, py_slope_px);

                let x0_sub = to_sub_pixel(x0);
                let x1_sub = to_sub_pixel(x1);
                let y0_sub = to_sub_pixel(y0);
                let y1_sub = to_sub_pixel(y1);

                if intersects_y_border(y0_sub, y1_sub) {
                    let y = y0_sub.max(y1_sub) >> PIXEL_SHIFT as i32;
                    let y_sub = y << PIXEL_SHIFT as i32;

                    let x = lines.slopes_recip[li].mul_add(y as f32, px_slope_recip_py);
                    let x_sub = to_sub_pixel(x);

                    let border_y = y_sub.min(y0_sub) >> PIXEL_SHIFT as i32;
                    let segment0 =
                        segment(layer, x0_sub, x_sub, y0_sub, y_sub, border_x, border_y, octant);

                    let border_y = y_sub.min(y1_sub) >> PIXEL_SHIFT as i32;
                    let segment1 =
                        segment(layer, x_sub, x1_sub, y_sub, y1_sub, border_x, border_y, octant);

                    [segment0, segment1]
                } else {
                    let border_y = y0_sub.min(y1_sub) >> PIXEL_SHIFT as i32;
                    let segment0 =
                        segment(layer, x0_sub, x1_sub, y0_sub, y1_sub, border_x, border_y, octant);

                    [segment0, CompactSegment::default()]
                }
            });

        self.segments.par_extend(par_iter);
    }

    pub fn sort(&mut self) {
        self.segments_mut()
            .par_sort_unstable_by_key(|segment| segment.unwrap() >> (16 + 2 * TILE_SHIFT));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::{rasterizer::raster_segment::RasterSegment, Point, Segment, TILE_SIZE};

    #[test]
    fn lengths_to_indices() {
        let lengths = [1, 2, 3, 1, 2, 3];
        let mut lines_indices = vec![];
        lines_indices.resize_uninit(12);
        let mut pixel_indices = vec![];
        pixel_indices.resize_uninit(12);

        populate_indices(&lengths, &mut lines_indices, &mut pixel_indices);

        assert_eq!(unsafe { lines_indices.assume_init() }, [0, 1, 1, 2, 2, 2, 3, 4, 4, 5, 5, 5]);
        assert_eq!(unsafe { pixel_indices.assume_init() }, [0, 0, 1, 0, 1, 2, 0, 0, 1, 0, 1, 2]);
    }

    fn segments(p0: Point<f32>, p1: Point<f32>) -> Vec<CompactSegment> {
        let mut builder = LinesBuilder::new();
        builder.push(0, &Segment::new(p0, p1));
        let lines = builder.build(|_| None);

        let mut rasterizer = Rasterizer::default();
        rasterizer.rasterize(&lines);

        rasterizer.segments().to_vec()
    }

    fn areas_and_covers(segments: &[CompactSegment]) -> Vec<Option<(i16, i8)>> {
        segments
            .iter()
            .map(|&segment| {
                let raster_segment: Option<RasterSegment> = segment.into();
                raster_segment.map(|segment| (segment.area, segment.cover))
            })
            .collect()
    }

    #[test]
    fn area_cover_octant_1() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(3.0, 2.0))),
            [
                Some((11 * 16 / 2, 11)),
                None,
                Some((5 * 8 / 2 + 5 * 8, 5)),
                Some((5 * 8 / 2, 5)),
                Some((11 * 16 / 2, 11)),
                None,
            ],
        );
    }

    #[test]
    fn area_cover_octant_2() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(2.0, 3.0))),
            [
                Some((16 * 11 / 2 + 16 * 5, 16)),
                None,
                Some((8 * 5 / 2, 8)),
                Some((8 * 5 / 2 + 8 * 11, 8)),
                Some((16 * 11 / 2, 16)),
                None,
            ],
        );
    }

    #[test]
    fn area_cover_octant_3() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-2.0, 3.0))),
            [
                Some((16 * 11 / 2, 16)),
                None,
                Some((8 * 5 / 2 + 8 * 11, 8)),
                Some((8 * 5 / 2, 8)),
                Some((16 * 11 / 2 + 16 * 5, 16)),
                None,
            ],
        );
    }

    #[test]
    fn area_cover_octant_4() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-3.0, 2.0))),
            [
                Some((11 * 16 / 2, 11)),
                None,
                Some((5 * 8 / 2 + 5 * 8, 5)),
                Some((5 * 8 / 2, 5)),
                Some((11 * 16 / 2, 11)),
                None,
            ],
        );
    }

    #[test]
    fn area_cover_octant_5() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-3.0, -2.0))),
            [
                Some((-(11 * 16 / 2), -11)),
                None,
                Some((-(5 * 8 / 2 + 5 * 8), -5)),
                Some((-(5 * 8 / 2), -5)),
                Some((-(11 * 16 / 2), -11)),
                None,
            ],
        );
    }

    #[test]
    fn area_cover_octant_6() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-2.0, -3.0))),
            [
                Some((-(16 * 11 / 2 + 16 * 5), -16)),
                None,
                Some((-(8 * 5 / 2), -8)),
                Some((-(8 * 5 / 2 + 8 * 11), -8)),
                Some((-(16 * 11 / 2), -16)),
                None,
            ],
        );
    }

    #[test]
    fn area_cover_octant_7() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(2.0, -3.0))),
            [
                Some((-(16 * 11 / 2), -16)),
                None,
                Some((-(8 * 5 / 2 + 8 * 11), -8)),
                Some((-(8 * 5 / 2), -8)),
                Some((-(16 * 11 / 2 + 16 * 5), -16)),
                None,
            ],
        );
    }

    #[test]
    fn area_cover_octant_8() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(3.0, -2.0))),
            [
                Some((-(11 * 16 / 2), -11)),
                None,
                Some((-(5 * 8 / 2 + 5 * 8), -5)),
                Some((-(5 * 8 / 2), -5)),
                Some((-(11 * 16 / 2), -11)),
                None,
            ],
        );
    }

    #[test]
    fn area_cover_axis_0() {
        assert_eq!(areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(1.0, 0.0))), [],);
    }

    #[test]
    fn area_cover_axis_45() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(1.0, 1.0))),
            [Some((16 * 16 / 2, 16)), None],
        );
    }

    #[test]
    fn area_cover_axis_90() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(0.0, 1.0))),
            [Some((16 * 16, 16)), None],
        );
    }

    #[test]
    fn area_cover_axis_135() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-1.0, 1.0))),
            [Some((16 * 16 / 2, 16)), None],
        );
    }

    #[test]
    fn area_cover_axis_180() {
        assert_eq!(areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-1.0, 0.0))), [],);
    }

    #[test]
    fn area_cover_axis_225() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-1.0, -1.0))),
            [Some((-(16 * 16 / 2), -16)), None],
        );
    }

    #[test]
    fn area_cover_axis_270() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(0.0, -1.0))),
            [Some((-(16 * 16), -16)), None],
        );
    }

    #[test]
    fn area_cover_axis_315() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(-1.0, -1.0))),
            [Some((-(16 * 16 / 2), -16)), None],
        );
    }

    fn tiles(segments: &[CompactSegment]) -> Vec<Option<(i16, i16, u8, u8)>> {
        segments
            .iter()
            .map(|&segment| {
                let raster_segment: Option<RasterSegment> = segment.into();
                raster_segment
                    .map(|segment| (segment.tile_i, segment.tile_j, segment.tile_x, segment.tile_y))
            })
            .collect()
    }

    #[test]
    fn tile_octant_1() {
        assert_eq!(
            tiles(&segments(
                Point::new(TILE_SIZE as f32, TILE_SIZE as f32),
                Point::new(TILE_SIZE as f32 + 3.0, TILE_SIZE as f32 + 2.0),
            )),
            [
                Some((1, 1, 0, 0)),
                None,
                Some((1, 1, 1, 0)),
                Some((1, 1, 1, 1)),
                Some((1, 1, 2, 1)),
                None,
            ],
        );
    }

    #[test]
    fn tile_octant_2() {
        assert_eq!(
            tiles(&segments(
                Point::new(TILE_SIZE as f32, TILE_SIZE as f32),
                Point::new(TILE_SIZE as f32 + 2.0, TILE_SIZE as f32 + 3.0),
            )),
            [
                Some((1, 1, 0, 0)),
                None,
                Some((1, 1, 0, 1)),
                Some((1, 1, 1, 1)),
                Some((1, 1, 1, 2)),
                None,
            ],
        );
    }

    #[test]
    fn tile_octant_3() {
        assert_eq!(
            tiles(&segments(
                Point::new(-(TILE_SIZE as f32), TILE_SIZE as f32),
                Point::new(-(TILE_SIZE as f32) - 2.0, TILE_SIZE as f32 + 3.0),
            )),
            [
                Some((-2, 1, TILE_SIZE as u8 - 1, 0)),
                None,
                Some((-2, 1, TILE_SIZE as u8 - 1, 1)),
                Some((-2, 1, TILE_SIZE as u8 - 2, 1)),
                Some((-2, 1, TILE_SIZE as u8 - 2, 2)),
                None,
            ],
        );
    }

    #[test]
    fn tile_octant_4() {
        assert_eq!(
            tiles(&segments(
                Point::new(-(TILE_SIZE as f32), TILE_SIZE as f32),
                Point::new(-(TILE_SIZE as f32) - 3.0, TILE_SIZE as f32 + 2.0),
            )),
            [
                Some((-2, 1, TILE_SIZE as u8 - 3, 1)),
                None,
                Some((-2, 1, TILE_SIZE as u8 - 2, 1)),
                Some((-2, 1, TILE_SIZE as u8 - 2, 0)),
                Some((-2, 1, TILE_SIZE as u8 - 1, 0)),
                None,
            ],
        );
    }

    #[test]
    fn tile_octant_5() {
        assert_eq!(
            tiles(&segments(
                Point::new(-(TILE_SIZE as f32), -(TILE_SIZE as f32)),
                Point::new(-(TILE_SIZE as f32) - 3.0, -(TILE_SIZE as f32) - 2.0),
            )),
            [
                Some((-2, -2, TILE_SIZE as u8 - 3, TILE_SIZE as u8 - 2)),
                None,
                Some((-2, -2, TILE_SIZE as u8 - 2, TILE_SIZE as u8 - 2)),
                Some((-2, -2, TILE_SIZE as u8 - 2, TILE_SIZE as u8 - 1)),
                Some((-2, -2, TILE_SIZE as u8 - 1, TILE_SIZE as u8 - 1)),
                None,
            ],
        );
    }

    #[test]
    fn tile_octant_6() {
        assert_eq!(
            tiles(&segments(
                Point::new(-(TILE_SIZE as f32), -(TILE_SIZE as f32)),
                Point::new(-(TILE_SIZE as f32) - 2.0, -(TILE_SIZE as f32) - 3.0),
            )),
            [
                Some((-2, -2, TILE_SIZE as u8 - 2, TILE_SIZE as u8 - 3)),
                None,
                Some((-2, -2, TILE_SIZE as u8 - 2, TILE_SIZE as u8 - 2)),
                Some((-2, -2, TILE_SIZE as u8 - 1, TILE_SIZE as u8 - 2)),
                Some((-2, -2, TILE_SIZE as u8 - 1, TILE_SIZE as u8 - 1)),
                None,
            ],
        );
    }

    #[test]
    fn tile_octant_7() {
        assert_eq!(
            tiles(&segments(
                Point::new(TILE_SIZE as f32, -(TILE_SIZE as f32)),
                Point::new(TILE_SIZE as f32 + 2.0, -(TILE_SIZE as f32) - 3.0),
            )),
            [
                Some((1, -2, 1, TILE_SIZE as u8 - 3)),
                None,
                Some((1, -2, 1, TILE_SIZE as u8 - 2)),
                Some((1, -2, 0, TILE_SIZE as u8 - 2)),
                Some((1, -2, 0, TILE_SIZE as u8 - 1)),
                None,
            ],
        );
    }

    #[test]
    fn tile_octant_8() {
        assert_eq!(
            tiles(&segments(
                Point::new(TILE_SIZE as f32, -(TILE_SIZE as f32)),
                Point::new(TILE_SIZE as f32 + 3.0, -(TILE_SIZE as f32) - 2.0),
            )),
            [
                Some((1, -2, 0, TILE_SIZE as u8 - 1)),
                None,
                Some((1, -2, 1, TILE_SIZE as u8 - 1)),
                Some((1, -2, 1, TILE_SIZE as u8 - 2)),
                Some((1, -2, 2, TILE_SIZE as u8 - 2)),
                None,
            ],
        );
    }

    #[test]
    fn start_and_end_not_on_pixel_border() {
        assert_eq!(
            areas_and_covers(&segments(Point::new(0.5, 0.25), Point::new(4.0, 2.0)))[0],
            Some((4 * 8 / 2, 4)),
        );

        assert_eq!(
            areas_and_covers(&segments(Point::new(0.0, 0.0), Point::new(3.5, 1.75)))[6],
            Some((4 * 8 / 2 + 4 * 8, 4)),
        );
    }
}
