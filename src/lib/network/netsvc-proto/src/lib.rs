// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate contains facilities to interact with netsvc over the
//! network.

use packet::BufferViewMut;

pub mod netboot;

/// Helper to convince the compiler we're holding buffer views.
fn as_buffer_view_mut<'a, B: BufferViewMut<&'a mut [u8]>>(
    v: B,
) -> impl BufferViewMut<&'a mut [u8]> {
    v
}
