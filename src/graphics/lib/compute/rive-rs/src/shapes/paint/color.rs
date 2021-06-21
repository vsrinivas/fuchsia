// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

#[derive(Clone, Copy, Default, Eq, PartialEq)]
pub struct Color32(pub u32);

impl Color32 {
    pub fn new(val: u32) -> Self {
        Self(val)
    }

    pub fn from_argb(alpha: u8, red: u8, green: u8, blue: u8) -> Self {
        Self((alpha as u32) << 24 | (red as u32) << 16 | (green as u32) << 8 | blue as u32)
    }

    pub fn alpha(self) -> u8 {
        ((0xFF000000 & self.0) >> 24) as u8
    }

    pub fn red(self) -> u8 {
        ((0x00FF0000 & self.0) >> 16) as u8
    }

    pub fn green(self) -> u8 {
        ((0x0000FF00 & self.0) >> 8) as u8
    }

    pub fn blue(self) -> u8 {
        (0x000000FF & self.0) as u8
    }

    pub fn opacity(self) -> f32 {
        self.alpha() as f32 / u8::MAX as f32
    }

    pub fn with_alpha(self, alpha: u8) -> Self {
        Self::from_argb(alpha, self.red(), self.green(), self.blue())
    }

    pub fn with_opacity(self, opacity: f32) -> Self {
        self.with_alpha((opacity * 255.0).round() as u8)
    }

    pub fn mul_opacity(self, opacity: f32) -> Self {
        self.with_opacity(self.opacity() * opacity)
    }

    pub fn lerp(self, other: Self, ratio: f32) -> Self {
        fn lerp(a: u8, b: u8, ratio: f32) -> u8 {
            (a as f32 + ((b as f32 - a as f32) * ratio)).round() as u8
        }

        Self::from_argb(
            lerp(self.alpha(), other.alpha(), ratio),
            lerp(self.red(), other.red(), ratio),
            lerp(self.green(), other.green(), ratio),
            lerp(self.blue(), other.blue(), ratio),
        )
    }
}

impl fmt::Debug for Color32 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Color32")
            .field("alpha", &self.alpha())
            .field("red", &self.red())
            .field("green", &self.green())
            .field("blue", &self.blue())
            .finish()
    }
}

impl From<u32> for Color32 {
    fn from(val: u32) -> Self {
        Self(val)
    }
}
