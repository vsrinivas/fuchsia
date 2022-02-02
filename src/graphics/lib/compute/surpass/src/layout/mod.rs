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

mod splits_cache;

pub use splits_cache::{Sealed, SplitsCache};

/// Listener that gets called after every write to the buffer. Its main use is to flush freshly
/// written memory slices.
pub trait Flusher: fmt::Debug + Send + Sync {
    /// Called after `slice` was written to.
    fn flush(&self, slice: &mut [u8]);
}

/// A buffer's layout description.
///
/// Implementors are supposed to cache sub-slices between uses provided they are being used with
/// exactly the same buffer. The recommended way to do this is to store a [`SplitsCache`] in every
/// layout implementation.
pub trait Layout<'l, 'b> {
    /// A per-tile writer type that should be able to write colors to the buffer independently of
    /// all other writers.
    type Writer: TileWriter;

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

    /// Produces a new [`Layout::Writer`] from `slices` buffer sub-slices and `flusher`.
    ///
    /// # Examples
    ///
    /// ```
    /// # use surpass::layout::{Layout, LinearLayout, TileFill, TileWriter};
    /// let mut buffer = [
    ///     [1; 4], [2; 4], [3; 4],
    ///     [4; 4], [5; 4], [6; 4],
    /// ].concat();
    /// let mut layout = LinearLayout::new(2, 3 * 4, 4);
    /// let splits = layout.splits(&mut buffer);
    /// let mut writer = LinearLayout::writer(splits, None);
    ///
    /// writer.write(TileFill::Solid([0; 4]));
    ///
    /// assert_eq!(buffer, [
    ///     [0; 4], [0; 4], [3; 4],
    ///     [0; 4], [0; 4], [6; 4],
    /// ].concat());
    /// ```
    fn writer(slices: &'l mut [&'b mut [u8]], flusher: Option<&'b dyn Flusher>) -> Self::Writer;

    /// Returns self-stored sub-slices of `buffer`, commonly stored in a [`SplitsCache`].
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
    /// let splits = layout.splits(&mut buffer);
    ///
    /// assert_eq!(splits[0], &[[1; 4], [2; 4]].concat());
    /// assert_eq!(splits[1], &[[4; 4], [5; 4]].concat());
    /// ```
    fn splits(&'l mut self, buffer: &'b mut [u8]) -> &'l mut [&'b mut [u8]];

    /// Width in tiles.
    ///
    /// # Examples
    ///
    /// ```
    /// # use surpass::{layout::{Layout, LinearLayout}, TILE_SIZE};
    /// let layout = LinearLayout::new(2 * TILE_SIZE, 3 * TILE_SIZE, 4 * TILE_SIZE);
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
    /// let layout = LinearLayout::new(2 * TILE_SIZE, 3 * TILE_SIZE, 4 * TILE_SIZE);
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
    splits: SplitsCache<u8>,
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
    /// let layout = LinearLayout::new(2, 3, 4);
    ///
    /// assert_eq!(layout.width(), 2);
    /// ```
    #[inline]
    pub fn new(width: usize, width_stride: usize, height: usize) -> Self {
        let splits = SplitsCache::new(move |buffer| {
            let mut layout: Vec<_> = buffer
                .chunks_exact_mut(width_stride)
                .enumerate()
                .map(|(j, row)| {
                    row[..width * 4].chunks_mut(TILE_SIZE * 4).enumerate().map(move |(i, slice)| {
                        let j = j >> TILE_SHIFT;
                        (i, j, slice)
                    })
                })
                .flatten()
                .collect();
            layout.par_sort_by_key(|&(i, j, _)| (j, i));

            layout.into_iter().map(|(_, _, slice)| slice).collect()
        });

        LinearLayout { splits, width, width_stride, height }
    }
}

impl<'l, 'b: 'l> Layout<'l, 'b> for LinearLayout {
    type Writer = LinearTileWriter<'l, 'b>;

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
    fn splits(&'l mut self, buffer: &'b mut [u8]) -> &'l mut [&'b mut [u8]] {
        let width = self.width;
        let width_stride = self.width_stride;

        assert!(width <= buffer.len(), "width exceeds buffer length: {} > {}", width, buffer.len());
        assert!(
            width_stride <= buffer.len(),
            "width_stride exceeds buffer length: {} > {}",
            width_stride,
            buffer.len(),
        );

        self.splits.access(buffer)
    }

    #[inline]
    fn writer(slices: &'l mut [&'b mut [u8]], flusher: Option<&'b dyn Flusher>) -> Self::Writer {
        LinearTileWriter { rows: slices, flusher }
    }
}

/// A fill that the [`TileWriter`] uses to write to tiles.
pub enum TileFill<'c> {
    /// Fill tile with a solid color.
    Solid([u8; 4]),
    /// Fill tile with provided colors buffer. They are provided in [column-major] order.
    ///
    /// [column-major]: https://en.wikipedia.org/wiki/Row-_and_column-major_order
    Full(&'c [[u8; 4]]),
}

/// A per-tile writer produced by [`Layout`].
pub trait TileWriter {
    /// Writes a `fill` to the part of the buffer that corresponds to this tile.
    ///
    /// # Examples
    ///
    /// ```
    /// # use surpass::layout::{Layout, LinearLayout, TileFill, TileWriter};
    /// let mut buffer = [
    ///     [1; 4], [2; 4], [3; 4],
    ///     [4; 4], [5; 4], [6; 4],
    /// ].concat();
    /// let mut layout = LinearLayout::new(2, 3 * 4, 4);
    /// let splits = layout.splits(&mut buffer);
    /// let mut writer = LinearLayout::writer(splits, None);
    ///
    /// writer.write(TileFill::Solid([0; 4]));
    ///
    /// assert_eq!(buffer, [
    ///     [0; 4], [0; 4], [3; 4],
    ///     [0; 4], [0; 4], [6; 4],
    /// ].concat());
    fn write(&mut self, fill: TileFill<'_>);
}

/// [`LinearLayout`]'s [`Layout::Writer`].
#[derive(Debug)]
pub struct LinearTileWriter<'l, 'b> {
    rows: &'l mut [&'b mut [u8]],
    flusher: Option<&'b dyn Flusher>,
}

impl<'l, 'b> TileWriter for LinearTileWriter<'l, 'b> {
    #[inline]
    fn write(&mut self, fill: TileFill<'_>) {
        let tiles_len = self.rows.len();
        match fill {
            TileFill::Solid(solid) => {
                for row in self.rows.iter_mut().take(tiles_len) {
                    for color in row.chunks_exact_mut(4) {
                        color.copy_from_slice(&solid);
                    }
                }
            }
            TileFill::Full(colors) => {
                for (y, row) in self.rows.iter_mut().enumerate().take(tiles_len) {
                    for (x, color) in row.chunks_exact_mut(4).enumerate() {
                        color.copy_from_slice(&colors[x * TILE_SIZE + y]);
                    }
                }
            }
        }

        if let Some(flusher) = self.flusher {
            for row in self.rows.iter_mut().take(tiles_len) {
                flusher.flush(if let Some(subslice) = row.get_mut(..TILE_SIZE * 4) {
                    subslice
                } else {
                    *row
                });
            }
        }
    }
}
