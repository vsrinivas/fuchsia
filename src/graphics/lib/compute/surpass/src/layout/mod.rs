// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Buffer-layout-specific traits for user-defined behavior.
//!
//! The logic is split between two traits, the [`Layout`] and the [`TileWriter`]. [`Layout`]'s job
//! is to split a buffer into sub-slices that will then be distributed to tile to be rendered, and
//! to produces [`TileWriter`]s from some sub-range of the sub-slices. These writers can then write
//! color data to these sub-slices.

use std::fmt;

use rayon::prelude::*;

use crate::{TILE_SHIFT, TILE_SIZE};

mod slice_cache;
pub use slice_cache::{Ref, Slice, SliceCache, Span};

/// Listener that gets called after every write to the buffer. Its main use is to flush freshly
/// written memory slices.
pub trait Flusher: fmt::Debug + Send + Sync {
    /// Called after `slice` was written to.
    fn flush(&self, slice: &mut [u8]);
}

/// A fill that the [`Layout`] uses to write to tiles.
pub enum TileFill<'c> {
    /// Fill tile with a solid color.
    Solid([u8; 4]),
    /// Fill tile with provided colors buffer. They are provided in [column-major] order.
    ///
    /// [column-major]: https://en.wikipedia.org/wiki/Row-_and_column-major_order
    Full(&'c [[u8; 4]]),
}

/// A buffer's layout description.
///
/// Implementors are supposed to cache sub-slices between uses provided they are being used with
/// exactly the same buffer. This is achieved by storing a [`SliceCache`] in every layout
/// implementation.
pub trait Layout {
    /// Width in pixels.
    ///
    /// # Examples
    ///
    /// ```
    /// # use surpass::layout::{Layout, LinearLayout};
    /// let layout = LinearLayout::new(2, 3 * 4, 4);
    ///
    /// assert_eq!(layout.width(), 2);
    /// ```
    fn width(&self) -> usize;

    /// Height in pixels.
    ///
    /// # Examples
    ///
    /// ```
    /// # use surpass::layout::{Layout, LinearLayout};
    /// let layout = LinearLayout::new(2, 3 * 4, 4);
    ///
    /// assert_eq!(layout.height(), 4);
    /// ```
    fn height(&self) -> usize;

    /// Number of buffer sub-slices that will be passes to [`Layout::writer`].
    ///
    /// # Examples
    ///
    /// ```
    /// # use surpass::{layout::{Layout, LinearLayout}, TILE_SIZE};
    /// let layout = LinearLayout::new(2, 3 * 4, 4);
    ///
    /// assert_eq!(layout.slices_per_tile(), TILE_SIZE);
    /// ```
    fn slices_per_tile(&self) -> usize;

    /// Returns self-stored sub-slices of `buffer` which are stored in a [`SliceCache`].
    ///
    /// # Examples
    ///
    /// ```
    /// # use surpass::layout::{Layout, LinearLayout};
    /// let mut buffer = [
    ///     [1; 4], [2; 4], [3; 4],
    ///     [4; 4], [5; 4], [6; 4],
    /// ].concat();
    /// let mut layout = LinearLayout::new(2, 3 * 4, 2);
    /// let slices = layout.slices(&mut buffer);
    ///
    /// assert_eq!(&*slices[0], &[[1; 4], [2; 4]].concat());
    /// assert_eq!(&*slices[1], &[[4; 4], [5; 4]].concat());
    /// ```
    fn slices<'l, 'b>(&'l mut self, buffer: &'b mut [u8]) -> Ref<'l, [Slice<'b, u8>]>;

    /// Writes `fill` to `slices`, optionally calling the `flusher`.
    ///
    /// # Examples
    ///
    /// ```
    /// # use surpass::layout::{Layout, LinearLayout, TileFill};
    /// let mut buffer = [
    ///     [1; 4], [2; 4], [3; 4],
    ///     [4; 4], [5; 4], [6; 4],
    /// ].concat();
    /// let mut layout = LinearLayout::new(2, 3 * 4, 2);
    ///
    /// LinearLayout::write(&mut *layout.slices(&mut buffer), None, TileFill::Solid([0; 4]));
    ///
    /// assert_eq!(buffer, [
    ///     [0; 4], [0; 4], [3; 4],
    ///     [0; 4], [0; 4], [6; 4],
    /// ].concat());
    fn write(slices: &mut [Slice<'_, u8>], flusher: Option<&dyn Flusher>, fill: TileFill<'_>);

    /// Width in tiles.
    ///
    /// # Examples
    ///
    /// ```
    /// # use surpass::{layout::{Layout, LinearLayout}, TILE_SIZE};
    /// let layout = LinearLayout::new(2 * TILE_SIZE, 3 * TILE_SIZE * 4, 4 * TILE_SIZE);
    ///
    /// assert_eq!(layout.width_in_tiles(), 2);
    /// ```
    #[inline]
    fn width_in_tiles(&self) -> usize {
        (self.width() + TILE_SIZE - 1) >> TILE_SHIFT
    }

    /// Height in tiles.
    ///
    /// # Examples
    ///
    /// ```
    /// # use surpass::{layout::{Layout, LinearLayout}, TILE_SIZE};
    /// let layout = LinearLayout::new(2 * TILE_SIZE, 3 * TILE_SIZE * 4, 4 * TILE_SIZE);
    ///
    /// assert_eq!(layout.height_in_tiles(), 4);
    /// ```
    #[inline]
    fn height_in_tiles(&self) -> usize {
        (self.height() + TILE_SIZE - 1) >> TILE_SHIFT
    }
}

/// A linear buffer layout where each optionally strided pixel row of an image is saved
/// sequentially into the buffer.
#[derive(Debug)]
pub struct LinearLayout {
    cache: SliceCache,
    width: usize,
    width_stride: usize,
    height: usize,
}

impl LinearLayout {
    /// Creates a new linear layout from `width`, `width_stride` (in bytes) and `height`.
    ///
    /// # Examples
    ///
    /// ```
    /// # use surpass::layout::{Layout, LinearLayout};
    /// let layout = LinearLayout::new(2, 3 * 4, 4);
    ///
    /// assert_eq!(layout.width(), 2);
    /// ```
    #[inline]
    pub fn new(width: usize, width_stride: usize, height: usize) -> Self {
        assert!(
            width * 4 <= width_stride,
            "width exceeds width stride: {} * 4 > {}",
            width,
            width_stride
        );

        let cache = SliceCache::new(width_stride * height, move |buffer| {
            let mut layout: Vec<_> = buffer
                .chunks(width_stride)
                .enumerate()
                .map(|(j, row)| {
                    row.slice(..width * 4).unwrap().chunks(TILE_SIZE * 4).enumerate().map(
                        move |(i, slice)| {
                            let j = j >> TILE_SHIFT;
                            (i, j, slice)
                        },
                    )
                })
                .flatten()
                .collect();
            layout.par_sort_by_key(|&(i, j, _)| (j, i));

            layout.into_iter().map(|(_, _, slice)| slice).collect()
        });

        LinearLayout { cache, width, width_stride, height }
    }
}

impl Layout for LinearLayout {
    #[inline]
    fn width(&self) -> usize {
        self.width
    }

    #[inline]
    fn height(&self) -> usize {
        self.height
    }

    #[inline]
    fn slices_per_tile(&self) -> usize {
        TILE_SIZE
    }

    #[inline]
    fn slices<'l, 'b>(&'l mut self, buffer: &'b mut [u8]) -> Ref<'l, [Slice<'b, u8>]> {
        assert!(
            self.width <= buffer.len(),
            "width exceeds buffer length: {} > {}",
            self.width,
            buffer.len()
        );
        assert!(
            self.width_stride <= buffer.len(),
            "width_stride exceeds buffer length: {} > {}",
            self.width_stride,
            buffer.len(),
        );
        assert!(
            self.height * self.width_stride <= buffer.len(),
            "height * width_stride exceeds buffer length: {} > {}",
            self.height * self.width_stride,
            buffer.len(),
        );

        self.cache.access(buffer).unwrap()
    }

    #[inline]
    fn write(slices: &mut [Slice<'_, u8>], flusher: Option<&dyn Flusher>, fill: TileFill<'_>) {
        let tiles_len = slices.len();
        match fill {
            TileFill::Solid(solid) => {
                for row in slices.iter_mut().take(tiles_len) {
                    for color in row.chunks_exact_mut(4) {
                        color.copy_from_slice(&solid);
                    }
                }
            }
            TileFill::Full(colors) => {
                for (y, row) in slices.iter_mut().enumerate().take(tiles_len) {
                    for (x, color) in row.chunks_exact_mut(4).enumerate() {
                        color.copy_from_slice(&colors[x * TILE_SIZE + y]);
                    }
                }
            }
        }

        if let Some(flusher) = flusher {
            for row in slices.iter_mut().take(tiles_len) {
                flusher.flush(if let Some(subslice) = row.get_mut(..TILE_SIZE * 4) {
                    subslice
                } else {
                    &mut **row
                });
            }
        }
    }
}
