// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    carnelian::color::Color,
    std::str::FromStr,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ColorScheme {
    pub front: Color,
    pub back: Color,
}

const WHITE_COLOR: Color = Color { r: 255, g: 255, b: 255, a: 255 };
const BLACK_COLOR: Color = Color { r: 0, g: 0, b: 0, a: 255 };
const BLUE_COLOR: Color = Color { r: 0, g: 0, b: 170, a: 255 };

pub const DARK_COLOR_SCHEME: ColorScheme = ColorScheme { front: WHITE_COLOR, back: BLACK_COLOR };
pub const LIGHT_COLOR_SCHEME: ColorScheme = ColorScheme { front: BLACK_COLOR, back: WHITE_COLOR };
pub const SPECIAL_COLOR_SCHEME: ColorScheme = ColorScheme { front: WHITE_COLOR, back: BLUE_COLOR };

impl Default for ColorScheme {
    fn default() -> Self {
        DARK_COLOR_SCHEME
    }
}

impl FromStr for ColorScheme {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "dark" => Ok(DARK_COLOR_SCHEME),
            "light" => Ok(LIGHT_COLOR_SCHEME),
            "special" => Ok(SPECIAL_COLOR_SCHEME),
            "default" => Ok(ColorScheme::default()),
            _ => Err(anyhow!("Invalid ColorScheme {}", s)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn check_color_scheme_from_str() -> Result<(), Error> {
        let dark = ColorScheme::from_str("dark")?;
        assert_eq!(dark, DARK_COLOR_SCHEME);
        let light = ColorScheme::from_str("light")?;
        assert_eq!(light, LIGHT_COLOR_SCHEME);
        let special = ColorScheme::from_str("special")?;
        assert_eq!(special, SPECIAL_COLOR_SCHEME);
        let default = ColorScheme::from_str("default")?;
        assert_eq!(default, ColorScheme::default());
        let invalid = ColorScheme::from_str("invalid");
        assert_eq!(invalid.is_ok(), false);

        Ok(())
    }
}
