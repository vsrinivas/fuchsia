// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::core::TryFromU64;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BlendMode {
    SrcOver,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
    Multiply,
    Hue,
    Saturation,
    Color,
    Luminosity,
}

impl Default for BlendMode {
    fn default() -> Self {
        Self::SrcOver
    }
}

impl TryFromU64 for BlendMode {
    fn try_from(val: u64) -> Option<Self> {
        match val {
            3 => Some(Self::SrcOver),
            14 => Some(Self::Screen),
            15 => Some(Self::Overlay),
            16 => Some(Self::Darken),
            17 => Some(Self::Lighten),
            18 => Some(Self::ColorDodge),
            19 => Some(Self::ColorBurn),
            20 => Some(Self::HardLight),
            21 => Some(Self::SoftLight),
            22 => Some(Self::Difference),
            23 => Some(Self::Exclusion),
            24 => Some(Self::Multiply),
            25 => Some(Self::Hue),
            26 => Some(Self::Saturation),
            27 => Some(Self::Color),
            28 => Some(Self::Luminosity),
            _ => None,
        }
    }
}
