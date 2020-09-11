// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::RefCell,
    fmt,
    ops::Range,
    slice::{self, ChunksExactMut},
};

use rayon::prelude::*;

use crate::{
    painter::{Painter, Style},
    rasterizer::{search_last_by_key, CompactSegment},
    TILE_MASK, TILE_SHIFT, TILE_SIZE,
};

thread_local!(static PAINTER: RefCell<Painter> = RefCell::new(Painter::new()));

#[derive(Debug)]
pub struct TileSlice {
    ptr: *mut [u8; 4],
    len: usize,
}

impl TileSlice {
    #[inline]
    pub fn as_mut_slice(&mut self) -> &mut [[u8; 4]] {
        unsafe { slice::from_raw_parts_mut(self.ptr, self.len) }
    }
}

unsafe impl Send for TileSlice {}

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub struct Rect {
    pub(crate) horizontal: Range<usize>,
    pub(crate) vertical: Range<usize>,
}

impl Rect {
    pub fn new(horizontal: Range<usize>, vertical: Range<usize>) -> Self {
        Self {
            horizontal: horizontal.start / TILE_SIZE..(horizontal.end + TILE_SIZE - 1) / TILE_SIZE,
            vertical: vertical.start / TILE_SIZE..(vertical.end + TILE_SIZE - 1) / TILE_SIZE,
        }
    }
}

pub trait Flusher: Send + Sync {
    fn flush(&self, slice: &mut [[u8; 4]]);
}

pub struct BufferLayoutBuilder {
    width: usize,
    width_stride: usize,
    flusher: Option<Box<dyn Flusher>>,
}

impl BufferLayoutBuilder {
    #[inline]
    pub fn new(width: usize) -> Self {
        BufferLayoutBuilder { width, width_stride: width, flusher: None }
    }

    #[inline]
    pub fn set_width_stride(mut self, width_stride: usize) -> Self {
        self.width_stride = width_stride;
        self
    }

    #[inline]
    pub fn set_flusher(mut self, flusher: Box<dyn Flusher>) -> Self {
        self.flusher = Some(flusher);
        self
    }

    #[inline]
    pub fn build(self, buffer: &mut [[u8; 4]]) -> BufferLayout {
        let width = self.width;
        let width_stride = self.width_stride;
        let flusher = self.flusher;

        assert!(width <= buffer.len(), "width exceeds buffer length: {} > {}", width, buffer.len());
        assert!(
            width_stride <= buffer.len(),
            "width_stride exceeds buffer length: {} > {}",
            width_stride,
            buffer.len(),
        );

        let mut row_len = width >> TILE_SHIFT;
        if width & TILE_MASK != 0 {
            row_len += 1;
        }

        let mut layout: Vec<_> = buffer
            .chunks_mut(width_stride)
            .enumerate()
            .map(|(j, row)| {
                row[..width].chunks_mut(TILE_SIZE).enumerate().map(move |(i, slice)| {
                    let j = j >> TILE_SHIFT;
                    (i, j, TileSlice { ptr: slice.as_mut_ptr(), len: slice.len() })
                })
            })
            .flatten()
            .collect();
        layout.sort_by_key(|&(i, j, _)| (j, i));

        BufferLayout {
            ptr: buffer.as_mut_ptr(),
            len: buffer.len(),
            layout: layout.into_iter().map(|(_, _, slice)| slice).collect(),
            row_len,
            flusher,
        }
    }
}

pub struct BufferLayout {
    ptr: *mut [u8; 4],
    len: usize,
    layout: Vec<TileSlice>,
    row_len: usize,
    flusher: Option<Box<dyn Flusher>>,
}

impl fmt::Debug for BufferLayout {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt.debug_struct("BufferLayout")
            .field("ptr", &self.ptr)
            .field("len", &self.len)
            .field("layout", &self.layout)
            .field("row_len", &self.row_len)
            .field("flusher", &format_args!("{}", self.flusher.is_some()))
            .finish()
    }
}

impl BufferLayout {
    fn par_tile_rows(
        &mut self,
        f: impl Fn(usize, ChunksExactMut<'_, TileSlice>, Option<&Box<dyn Flusher>>) + Send + Sync,
    ) {
        let row_len = self.row_len;
        let flusher = self.flusher.as_ref();
        self.layout
            .par_chunks_mut(row_len * TILE_SIZE)
            .enumerate()
            .for_each(|(j, row)| f(j, row.chunks_exact_mut(row.len() / row_len), flusher));
    }

    #[inline]
    pub fn same_buffer(&self, buffer: &mut [[u8; 4]]) -> bool {
        buffer.as_mut_ptr() == self.ptr && buffer.len() == self.len
    }

    pub fn print<F>(
        &mut self,
        buffer: &mut [[u8; 4]],
        mut segments: &[CompactSegment],
        clear_color: [f32; 4],
        crop: Option<Rect>,
        styles: F,
    ) where
        F: Fn(u16) -> Style + Send + Sync,
    {
        if !self.same_buffer(buffer) {
            panic!(
                "BufferLayout::print called with a different buffer than the one than the\
                 BufferLayout was created with"
            );
        }

        if let Ok(end) =
            search_last_by_key(segments, false, |segment| segment.tile_j().is_negative())
        {
            segments = &segments[..=end];
        }

        self.par_tile_rows(|j, row, flusher| {
            if let Some(rect) = &crop {
                if !rect.vertical.contains(&j) {
                    return;
                }
            }

            if let Ok(end) = search_last_by_key(segments, j as i16, |segment| segment.tile_j()) {
                let result = search_last_by_key(segments, j as i16 - 1, |segment| segment.tile_j());
                let start = match result {
                    Ok(i) => i + 1,
                    Err(i) => i,
                };

                PAINTER.with(|painter| {
                    painter.borrow_mut().paint_tile_row(
                        &segments[start..=end],
                        &styles,
                        clear_color,
                        flusher,
                        row,
                        crop.clone(),
                    );
                    painter.borrow_mut().reset();
                });
            }
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::{collections::HashMap, iter};

    use crate::{
        painter::{BlendMode, Fill, FillRule, Style},
        PIXEL_WIDTH,
    };

    const BLACK: [f32; 4] = [0.0, 0.0, 0.0, 1.0];
    const RED: [f32; 4] = [1.0, 0.0, 0.0, 1.0];
    const GREEN: [f32; 4] = [0.0, 1.0, 0.0, 1.0];
    const BLUE: [f32; 4] = [0.0, 0.0, 1.0, 1.0];
    const BLACK_RGBA: [u8; 4] = [0, 0, 0, 255];
    const RED_RGBA: [u8; 4] = [255, 0, 0, 255];
    const GREEN_RGBA: [u8; 4] = [0, 255, 0, 255];
    const BLUE_RGBA: [u8; 4] = [0, 0, 255, 255];

    #[test]
    fn flusher() {
        macro_rules! seg {
            ( $j:expr, $i:expr ) => {
                CompactSegment::new(0, $j, $i, 0, 0, 0, 0, 0)
            };
        }

        struct WhiteFlusher;

        impl Flusher for WhiteFlusher {
            fn flush(&self, slice: &mut [[u8; 4]]) {
                for color in slice {
                    *color = [255u8; 4];
                }
            }
        }

        let size = TILE_SIZE + TILE_SIZE / 2;
        let mut buffer = vec![[0u8; 4]; size * size];
        let mut buffer_layout =
            BufferLayoutBuilder::new(size).set_flusher(Box::new(WhiteFlusher)).build(&mut buffer);

        buffer_layout.print(
            &mut buffer,
            &[seg!(0, 0), seg!(0, 1), seg!(1, 0), seg!(1, 1)],
            [0.0; 4],
            None,
            |_| Style::default(),
        );

        assert!(buffer.iter().all(|&color| color == [255u8; 4]));
    }

    #[test]
    fn skip_opaque_tiles() {
        let mut buffer = vec![[0u8; 4]; TILE_SIZE * TILE_SIZE * 3];
        let mut buffer_layout = BufferLayoutBuilder::new(TILE_SIZE * 3).build(&mut buffer);

        let mut segments = vec![];
        for y in 0..TILE_SIZE {
            segments.push(CompactSegment::new(
                0,
                0,
                -1,
                2,
                y as u8,
                TILE_SIZE as u8 - 1,
                0,
                PIXEL_WIDTH as i8,
            ));
        }

        segments.push(CompactSegment::new(
            0,
            0,
            -1,
            0,
            0,
            TILE_SIZE as u8 - 1,
            0,
            PIXEL_WIDTH as i8,
        ));
        segments.push(CompactSegment::new(0, 0, 0, 1, 1, 0, 0, PIXEL_WIDTH as i8));

        for y in 0..TILE_SIZE {
            segments.push(CompactSegment::new(
                0,
                0,
                1,
                2,
                y as u8,
                TILE_SIZE as u8 - 1,
                0,
                -(PIXEL_WIDTH as i8),
            ));
        }

        segments.sort();

        let mut styles = HashMap::new();

        styles.insert(
            0,
            Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(BLUE),
                blend_mode: BlendMode::Over,
            },
        );
        styles.insert(
            1,
            Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(GREEN),
                blend_mode: BlendMode::Over,
            },
        );
        styles.insert(
            2,
            Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(RED),
                blend_mode: BlendMode::Over,
            },
        );

        buffer_layout.print(&mut buffer, &segments, BLACK, None, |layer| styles[&layer]);

        let tiles: Vec<_> =
            buffer_layout.layout.iter_mut().map(|slice| slice.as_mut_slice().to_owned()).collect();

        assert_eq!(
            tiles,
            // First two tiles need to be completely red.
            iter::repeat(vec![RED_RGBA; TILE_SIZE])
                .take(TILE_SIZE)
                .chain(iter::repeat(vec![RED_RGBA; TILE_SIZE]).take(TILE_SIZE))
                .chain(
                    // The last tile contains one blue and one green line.
                    iter::once(vec![BLUE_RGBA; TILE_SIZE])
                        .chain(iter::once(vec![GREEN_RGBA; TILE_SIZE]))
                        // Followed by black lines (clear color).
                        .chain(iter::repeat(vec![BLACK_RGBA; TILE_SIZE]).take(TILE_SIZE - 2))
                )
                .collect::<Vec<_>>()
        );
    }

    #[test]
    fn crop() {
        let mut buffer = vec![[0u8; 4]; TILE_SIZE * TILE_SIZE * 9];
        let mut buffer_layout = BufferLayoutBuilder::new(TILE_SIZE * 3).build(&mut buffer);

        let mut segments = vec![];
        for j in 0..3 {
            for y in 0..TILE_SIZE {
                segments.push(CompactSegment::new(
                    0,
                    j,
                    0,
                    0,
                    y as u8,
                    TILE_SIZE as u8 - 1,
                    0,
                    PIXEL_WIDTH as i8,
                ));
            }
        }

        segments.sort();

        let mut styles = HashMap::new();

        styles.insert(
            0,
            Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(BLUE),
                blend_mode: BlendMode::Over,
            },
        );

        buffer_layout.print(
            &mut buffer,
            &segments,
            RED,
            Some(Rect::new(TILE_SIZE..TILE_SIZE * 2 + TILE_SIZE / 2, TILE_SIZE..TILE_SIZE * 2)),
            |layer| styles[&layer],
        );

        let tiles: Vec<_> =
            buffer_layout.layout.iter_mut().map(|slice| slice.as_mut_slice().to_owned()).collect();

        assert_eq!(
            tiles,
            // First row of tiles needs to be completely black.
            iter::repeat(vec![[0u8; 4]; TILE_SIZE])
                .take(TILE_SIZE * 3)
                // Second row begins with a black tile.
                .chain(iter::repeat(vec![[0u8; 4]; TILE_SIZE]).take(TILE_SIZE))
                .chain(iter::repeat(vec![BLUE_RGBA; TILE_SIZE]).take(TILE_SIZE * 2))
                // Third row of tiles needs to be completely black as well.
                .chain(iter::repeat(vec![[0u8; 4]; TILE_SIZE]).take(TILE_SIZE * 3))
                .collect::<Vec<_>>()
        );
    }
}
