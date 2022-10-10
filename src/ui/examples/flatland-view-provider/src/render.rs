// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_ui_composition as fland;

pub trait Renderer {
    // Fill 4 quadrants of the specified buffer with the 4 provided colors.
    // TODO(fxbug.dev/104692): this function should return a `zx::event` which will be signaled when
    // rendering is done, so that it can be as passed to `Flatland.PresentArgs` via
    // `PresentArgs.server_wait_fences`.
    fn render_rgba(&self, buffer_index: usize, colors: [[u8; 4]; 4]);

    // Provides an input token which can be used to share the buffer collection images between the
    // app to Flatland (or other Scenic APIs).
    fn duplicate_buffer_collection_import_token(&self) -> fland::BufferCollectionImportToken;
}
