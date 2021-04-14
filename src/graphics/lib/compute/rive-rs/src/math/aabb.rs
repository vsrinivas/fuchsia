// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::math::Vec;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Aabb {
    pub min: Vec,
    pub max: Vec,
}

impl Aabb {
    pub fn new(min_x: f32, min_y: f32, max_x: f32, max_y: f32) -> Self {
        Self { min: Vec::new(min_x, min_y), max: Vec::new(max_x, max_y) }
    }

    pub fn center(&self) -> Vec {
        Vec::new((self.min.x + self.max.x) * 0.5, (self.min.y + self.max.y) * 0.5)
    }

    pub fn size(&self) -> Vec {
        Vec::new(self.max.x - self.min.x, self.max.y - self.min.y)
    }

    pub fn extents(&self) -> Vec {
        self.size() * 0.5
    }
}
