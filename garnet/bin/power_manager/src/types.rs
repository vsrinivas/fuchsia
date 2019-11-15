// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Below are definitions for common types that may be used across Node boundaries (i.e.,
/// arguments within Messages). It may also be useful to define newtypes here to avoid
/// carrying around implicit unit measurement information (e.g., Celsius(f32)).

#[derive(Debug, Copy, Clone)]
pub struct Celsius(pub f64);

#[derive(Copy, Clone)]
pub struct Seconds(pub f64);
impl Seconds {
    pub fn from_nanos(nanos: i64) -> Seconds {
        Seconds(nanos as f64 / 1e9)
    }

    pub fn into_nanos(self) -> i64 {
        (self.0 * 1e9) as i64
    }
}

#[derive(Default, Copy, Clone)]
pub struct Nanoseconds(pub i64);

#[derive(Debug)]
pub struct Watts(pub f64);
