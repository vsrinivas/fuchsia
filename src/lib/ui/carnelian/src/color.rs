// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_ui_gfx::ColorRgba;

fn srgb_to_linear(l: u8) -> f32 {
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

    fn extract_hex_slice(hash_code: &str, start_index: usize) -> Result<u8, Error> {
        Ok(u8::from_str_radix(&hash_code[start_index..start_index + 2], 16)?)
    }

    /// Create a color from a six hexadecimal digit string like '#EBD5B3'
    pub fn from_hash_code(hash_code: &str) -> Result<Color, Error> {
        let mut new_color = Color::new();
        new_color.r = Color::extract_hex_slice(&hash_code, 1)?;
        new_color.g = Color::extract_hex_slice(&hash_code, 3)?;
        new_color.b = Color::extract_hex_slice(&hash_code, 5)?;
        Ok(new_color)
    }

    /// Create a Scenic-compatible [`ColorRgba`]
    pub fn make_color_rgba(&self) -> ColorRgba {
        ColorRgba { red: self.r, green: self.g, blue: self.b, alpha: self.a }
    }

    pub fn to_linear_premult_rgba(&self) -> [f32; 4] {
        let alpha = self.a as f32 * 255.0f32.recip();

        [
            srgb_to_linear(self.r) * alpha,
            srgb_to_linear(self.g) * alpha,
            srgb_to_linear(self.b) * alpha,
            alpha,
        ]
    }

    pub fn to_linear_bgra(&self) -> [f32; 4] {
        [
            srgb_to_linear(self.b),
            srgb_to_linear(self.g),
            srgb_to_linear(self.r),
            self.a as f32 * 255.0f32.recip(),
        ]
    }
}
