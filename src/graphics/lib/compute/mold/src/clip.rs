// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
/// A rectangle clip in screen space.
pub struct Clip {
    /// Rectangle's left value on the Ox axis, in pixels.
    pub x: usize,
    /// Rectangle's bottom value on the Ox axis, in pixels.
    pub y: usize,
    /// Rectangle's width, in pixels.
    pub width: usize,
    /// Rectangle's height, in pixels.
    pub height: usize,
}
