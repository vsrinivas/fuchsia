// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Clone, Debug)]
pub struct Layer {
    pub is_enabled: bool,
    pub affine_transform: Option<[f32; 6]>,
    pub order: Option<u16>,
}

impl Default for Layer {
    fn default() -> Self {
        Self { is_enabled: true, affine_transform: None, order: None }
    }
}
