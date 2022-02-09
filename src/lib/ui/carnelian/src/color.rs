// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_ui_gfx::ColorRgba;

pub(crate) fn srgb_to_linear(l: u8) -> f32 {
    let l = l as f32 * 255.0f32.recip();

    if l <= 0.04045 {
        l * 12.92f32.recip()
    } else {
        ((l + 0.055) * 1.055f32.recip()).powf(2.4)
    }
}

/// Struct representing an RGBA color value in un-premultiplied non-linear sRGB space
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
pub struct Color {
    /// Red
    pub r: u8,
    /// Green
    pub g: u8,
    /// Blue
    pub b: u8,
    /// Alpha
    pub a: u8,
}

impl Color {
    /// Create a new color set to black with full alpha
    pub const fn new() -> Color {
        Color { r: 0, g: 0, b: 0, a: 255 }
    }

    /// Create a new color set to white with full alpha
    pub const fn white() -> Color {
        Color { r: 255, g: 255, b: 255, a: 255 }
    }

    /// Create a new color set to red with full alpha
    pub const fn red() -> Color {
        Color { r: 255, g: 0, b: 0, a: 255 }
    }

    /// Create a new color set to green with full alpha
    pub const fn green() -> Color {
        Color { r: 0, g: 255, b: 0, a: 255 }
    }

    /// Create a new color set to blue with full alpha
    pub const fn blue() -> Color {
        Color { r: 0, g: 0, b: 255, a: 255 }
    }

    /// Create a new color set to Fuchsia with full alpha
    pub const fn fuchsia() -> Color {
        Color { r: 255, g: 0, b: 255, a: 255 }
    }

    fn extract_hex_slice(hash_code: &str, start_index: usize) -> Result<u8, Error> {
        Ok(u8::from_str_radix(&hash_code[start_index..start_index + 2], 16)?)
    }

    /// Create a color from a six or height hexadecimal digit string like '#EBD5B3'
    pub fn from_hash_code(hash_code: &str) -> Result<Color, Error> {
        let mut new_color = Color::new();
        new_color.r = Color::extract_hex_slice(&hash_code, 1)?;
        new_color.g = Color::extract_hex_slice(&hash_code, 3)?;
        new_color.b = Color::extract_hex_slice(&hash_code, 5)?;
        if hash_code.len() > 8 {
            new_color.a = Color::extract_hex_slice(&hash_code, 7)?;
        }
        Ok(new_color)
    }

    /// Create a Scenic-compatible [`ColorRgba`]
    pub fn make_color_rgba(&self) -> ColorRgba {
        ColorRgba { red: self.r, green: self.g, blue: self.b, alpha: self.a }
    }

    /// Create a linear, premultiplied RGBA representation
    pub fn to_linear_premult_rgba(&self) -> [f32; 4] {
        let alpha = self.a as f32 * 255.0f32.recip();

        [
            srgb_to_linear(self.r) * alpha,
            srgb_to_linear(self.g) * alpha,
            srgb_to_linear(self.b) * alpha,
            alpha,
        ]
    }

    /// Create a linear BGRA representation
    pub fn to_linear_bgra(&self) -> [f32; 4] {
        [
            srgb_to_linear(self.b),
            srgb_to_linear(self.g),
            srgb_to_linear(self.r),
            self.a as f32 * 255.0f32.recip(),
        ]
    }

    /// Create a premultiplied SRGB representation
    pub fn to_srgb_premult_rgba(&self) -> [f32; 4] {
        let recip = 255.0f32.recip();
        let alpha = self.a as f32 * recip;

        [
            self.r as f32 * recip * alpha,
            self.g as f32 * recip * alpha,
            self.b as f32 * recip * alpha,
            alpha,
        ]
    }
}

impl Default for Color {
    fn default() -> Self {
        Color::new()
    }
}
