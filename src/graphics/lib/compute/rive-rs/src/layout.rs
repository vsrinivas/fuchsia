// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::math::{self, Aabb, Mat};

#[derive(Clone, Copy, Debug)]
pub enum Fit {
    Fill,
    Contain,
    Cover,
    FitWidth,
    FitHeight,
    None,
    ScaleDown,
}

#[derive(Clone, Copy, Debug)]
pub struct Alignment(math::Vec);

impl Alignment {
    pub const fn new(x: f32, y: f32) -> Self {
        Self(math::Vec::new(x, y))
    }

    pub const fn top_left() -> Self {
        Self::new(-1.0, -1.0)
    }

    pub const fn top_center() -> Self {
        Self::new(0.0, -1.0)
    }

    pub const fn top_right() -> Self {
        Self::new(1.0, -1.0)
    }

    pub const fn center_left() -> Self {
        Self::new(-1.0, 0.0)
    }

    pub const fn center() -> Self {
        Self::new(0.0, 0.0)
    }

    pub const fn center_right() -> Self {
        Self::new(1.0, 0.0)
    }

    pub const fn bottom_left() -> Self {
        Self::new(-1.0, 1.0)
    }

    pub const fn bottom_center() -> Self {
        Self::new(0.0, 1.0)
    }

    pub const fn bottom_right() -> Self {
        Self::new(1.0, 1.0)
    }
}

pub fn align(fit: Fit, alignment: Alignment, frame: Aabb, content: Aabb) -> Mat {
    let content_size = content.size();
    let conent_half_size = content_size * 0.5;

    let pos = -content.min - conent_half_size - alignment.0 * conent_half_size;

    let scale_vec = match fit {
        Fit::Fill => frame.size() / content_size,
        Fit::Contain => {
            let scaled = frame.size() / content_size;
            let min_scale = scaled.x.min(scaled.y);

            math::Vec::new(min_scale, min_scale)
        }
        Fit::Cover => {
            let scaled = frame.size() / content_size;
            let max_scale = scaled.x.max(scaled.y);

            math::Vec::new(max_scale, max_scale)
        }
        Fit::FitWidth => {
            let min_scale = frame.size().x / content_size.x;

            math::Vec::new(min_scale, min_scale)
        }
        Fit::FitHeight => {
            let min_scale = frame.size().y / content_size.y;

            math::Vec::new(min_scale, min_scale)
        }
        Fit::None => math::Vec::new(1.0, 1.0),
        Fit::ScaleDown => {
            let scaled = frame.size() / content_size;
            let min_scale = scaled.x.min(scaled.y).min(1.0);

            math::Vec::new(min_scale, min_scale)
        }
    };

    let frame_half_size = frame.size() * 0.5;
    let translation_vec = frame.min + frame_half_size + alignment.0 * frame_half_size;

    let translation = Mat {
        translate_x: translation_vec.x,
        translate_y: translation_vec.y,
        ..Default::default()
    };

    let scale = Mat { scale_x: scale_vec.x, scale_y: scale_vec.y, ..Default::default() };

    let translation_back = Mat { translate_x: pos.x, translate_y: pos.y, ..Default::default() };

    translation * scale * translation_back
}
