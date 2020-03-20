// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::RefCell,
    fmt,
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
                    );
                    painter.borrow_mut().reset();
                });
            }
        });
    }
}
