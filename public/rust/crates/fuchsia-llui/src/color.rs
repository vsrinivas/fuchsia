// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Copy, Clone, Debug, Default, PartialEq)]
pub struct Color {
    pub red: f64,
    pub green: f64,
    pub blue: f64,
    pub alpha: f64,
}

impl Color {
    pub fn new() -> Color {
        Color {
            red: 0.0,
            green: 0.0,
            blue: 0.0,
            alpha: 1.0,
        }
    }

    pub fn extract_hex_slice(hash_code: &str, start_index: usize) -> u8 {
        u8::from_str_radix(&hash_code[start_index..start_index + 2], 16).unwrap()
    }

    pub fn convert_to_float_color_component(component: u8) -> f64 {
        (f64::from(component) * 100.0 / 255.0).round() / 100.0
    }

    pub fn from_hash_code(hash_code: &str) -> Color {
        let mut new_color = Color::new();
        new_color.red =
            Color::convert_to_float_color_component(Color::extract_hex_slice(hash_code, 1));
        new_color.green =
            Color::convert_to_float_color_component(Color::extract_hex_slice(hash_code, 3));
        new_color.blue =
            Color::convert_to_float_color_component(Color::extract_hex_slice(hash_code, 5));
        new_color
    }

    pub fn to_565(&self) -> [u8; 2] {
        let five_bit_mask = 0b1_1111;
        let six_bit_mask = 0b11_1111;
        let red: u8 = (f64::from(five_bit_mask) * self.red) as u8;
        let green: u8 = (f64::from(six_bit_mask) * self.green) as u8;
        let blue: u8 = (f64::from(five_bit_mask) * self.blue) as u8;
        let b1 = (red << 3) | ((green & 0b11_1000) >> 3);
        let b2 = ((green & 0b111) << 5) | blue;
        [b2, b1]
    }

    pub fn to_8888(&self) -> [u8; 4] {
        let red: u8 = (255.0 * self.red) as u8;
        let green: u8 = (255.0 * self.green) as u8;
        let blue: u8 = (255.0 * self.blue) as u8;
        let alpha: u8 = (255.0 * self.alpha) as u8;
        [blue, green, red, alpha]
    }

    pub fn scale(&self, amount: f64) -> Color {
        Color {
            red: self.red * amount,
            green: self.green * amount,
            blue: self.blue * amount,
            alpha: self.alpha * amount,
        }
    }
}
