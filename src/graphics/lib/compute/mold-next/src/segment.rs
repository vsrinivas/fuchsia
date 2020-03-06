// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::point::Point;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Segment<T> {
    pub p0: Point<T>,
    pub p1: Point<T>,
}

impl<T> Segment<T> {
    pub fn new(p0: Point<T>, p1: Point<T>) -> Self {
        Self { p0, p1 }
    }
}
