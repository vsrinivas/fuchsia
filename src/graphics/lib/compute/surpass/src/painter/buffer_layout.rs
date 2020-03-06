// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::RefCell,
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
    pub fn as_mut_slice(&mut self) -> &mut [[u8; 4]] {
        unsafe { slice::from_raw_parts_mut(self.ptr, self.len) }
    }
}

unsafe impl Send for TileSlice {}

#[derive(Debug)]
pub struct BufferLayout {
    ptr: *mut [u8; 4],
    len: usize,
    layout: Vec<TileSlice>,
    row_len: usize,
}

impl BufferLayout {
    #[inline]
    pub fn new(buffer: &mut [[u8; 4]], width: usize) -> Self {
        Self::with_stride(buffer, width, width)
    }

    #[inline]
    pub fn with_stride(buffer: &mut [[u8; 4]], width: usize, width_stride: usize) -> Self {
        assert!(width <= buffer.len(), "width exceeds buffer length: {} > {}", width, buffer.len());
        assert!(
            width_stride <= buffer.len(),
            "width_stride exceeds buffer length: {} > {}",
            width_stride,
            buffer.len()
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

        Self {
            ptr: buffer.as_mut_ptr(),
            len: buffer.len(),
            layout: layout.into_iter().map(|(_, _, slice)| slice).collect(),
            row_len,
        }
    }

    fn par_tile_rows(&mut self, f: impl Fn(usize, ChunksExactMut<'_, TileSlice>) + Send + Sync) {
        let row_len = self.row_len;
        self.layout
            .par_chunks_mut(row_len * TILE_SIZE)
            .enumerate()
            .for_each(|(j, row)| f(j, row.chunks_exact_mut(row.len() / row_len)));
    }

    #[inline]
    pub fn same_buffer(&self, buffer: &mut [[u8; 4]]) -> bool {
        buffer.as_mut_ptr() == self.ptr && buffer.len() == self.len
    }

    pub fn print<F>(
        &mut self,
        buffer: &mut [[u8; 4]],
        mut segments: &[CompactSegment],
        clear_color: [u8; 4],
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

        self.par_tile_rows(|j, row| {
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
                        row,
                    );
                    painter.borrow_mut().reset();
                });
            }
        });
    }
}
