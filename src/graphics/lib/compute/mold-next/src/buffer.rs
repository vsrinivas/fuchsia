// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use surpass::painter::BufferLayout;

#[derive(Debug)]
pub struct Buffer<'b> {
    pub buffer: &'b mut [[u8; 4]],
    pub width: usize,
    pub width_stride: Option<usize>,
}

impl Buffer<'_> {
    pub(crate) fn generate_layout(&mut self) -> BufferLayout {
        match self.width_stride {
            Some(width_stride) => BufferLayout::with_stride(self.buffer, self.width, width_stride),
            None => BufferLayout::new(self.buffer, self.width),
        }
    }
}
