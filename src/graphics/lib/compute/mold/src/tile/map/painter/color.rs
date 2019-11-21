// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::byte_fraction::ByteFraction;

pub const ZERO: Color = Color {
    red: ByteFraction::zero(),
    green: ByteFraction::zero(),
    blue: ByteFraction::zero(),
    alpha: ByteFraction::zero(),
};

pub const BLACK: Color = Color {
    red: ByteFraction::zero(),
    green: ByteFraction::zero(),
    blue: ByteFraction::zero(),
    alpha: ByteFraction::one(),
};

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Color {
    pub blue: ByteFraction,
    pub green: ByteFraction,
    pub red: ByteFraction,
    pub alpha: ByteFraction,
}

impl Color {
    pub fn swap_rb(self) -> Self {
        Self { blue: self.red, green: self.green, red: self.blue, alpha: self.alpha }
    }

    pub fn to_rgb565(self) -> u16 {
        let red = u16::from((self.alpha * self.red).value());
        let green = u16::from((self.alpha * self.green).value());
        let blue = u16::from((self.alpha * self.blue).value());

        let red = ((red >> 3) & 0b1_1111) << 11;
        let green = ((green >> 2) & 0b111_1111) << 5;
        let blue = (blue >> 3) & 0b1_1111;

        red | green | blue
    }
}
