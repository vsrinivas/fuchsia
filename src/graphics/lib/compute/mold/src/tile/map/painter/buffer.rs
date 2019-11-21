// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::mem;

/// Pixel format used in render output.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PixelFormat {
    /// red - 8bit, green - 8bit, blue - 8bit, alpha - 8bit
    RGBA8888,
    /// blue - 8bit, green - 8bit, red - 8bit, alpha - 8bit
    BGRA8888,
    /// red - 5bit, green - 6bit, blue - 5bit
    RGB565,
}

/// A color buffer for mold to write into.
///
/// It must be safe to write to non-overlapping parts of this buffer from multiple threads in
/// parallel.
///
/// # Examples
/// ```
/// # use std::ptr;
/// # use crate::mold::{ColorBuffer, PixelFormat};
/// #[derive(Clone, Debug)]
/// struct BitMap {
///     buffer: *mut u8,
///     width: usize,
/// }
///
/// unsafe impl Send for BitMap {}
/// unsafe impl Sync for BitMap {}
///
/// impl ColorBuffer for BitMap {
///     fn pixel_format(&self) -> PixelFormat {
///         PixelFormat::RGBA8888
///     }
///
///     fn stride(&self) -> usize {
///         self.width
///     }
///
///     unsafe fn write_at(&mut self, offset: usize, src: *const u8, len: usize) {
///         let dst = self.buffer.add(offset);
///         ptr::copy_nonoverlapping(src, dst, len);
///     }
/// }
/// ```
pub trait ColorBuffer: Clone + Send + Sync {
    /// Return the buffer's pixel format.
    fn pixel_format(&self) -> PixelFormat;

    /// Return the buffer's stride.
    fn stride(&self) -> usize;

    /// Write `len` bytes in `src` offset by `offset`.
    unsafe fn write_at(&mut self, offset: usize, src: *const u8, len: usize);

    #[doc(hidden)]
    unsafe fn write_color_at<C: Copy + Sized>(&mut self, offset: usize, src: &[C]) {
        let size = mem::size_of::<C>();
        self.write_at(offset * size, src.as_ptr() as *const u8, src.len() * size);
    }
}