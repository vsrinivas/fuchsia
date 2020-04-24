// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use surpass::painter::Flusher;
use surpass::painter::{BufferLayout, BufferLayoutBuilder};

pub struct Buffer<'b> {
    pub buffer: &'b mut [[u8; 4]],
    pub width: usize,
    pub width_stride: Option<usize>,
    pub flusher: Option<Box<dyn Flusher>>,
}

impl Buffer<'_> {
    pub(crate) fn generate_layout(&mut self) -> BufferLayout {
        let mut builder = BufferLayoutBuilder::new(self.width);
        if let Some(width_stride) = self.width_stride {
            builder = builder.set_width_stride(width_stride);
        }
        if let Some(flusher) = self.flusher.take() {
            builder = builder.set_flusher(flusher);
        }
        builder.build(self.buffer)
    }
}

impl Default for Buffer<'_> {
    fn default() -> Self {
        Self { buffer: &mut [], width: 0, width_stride: None, flusher: None }
    }
}
