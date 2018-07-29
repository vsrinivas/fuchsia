// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Copyright 2016 Joe Wilm, The Alacritty Project Contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use std::ops::Add;

/// Character used for the underline cursor
// This is part of the private use area and should not conflict with any font
pub const UNDERLINE_CURSOR_CHAR: char = '\u{10a3e2}';

/// Character used for the beam cursor
// This is part of the private use area and should not conflict with any font
pub const BEAM_CURSOR_CHAR: char = '\u{10a3e3}';

/// Character used for the empty box cursor
// This is part of the private use area and should not conflict with any font
pub const BOX_CURSOR_CHAR: char = '\u{10a3e4}';

/// Font size stored as integer
#[derive(Debug, Copy, Clone, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct Size(i16);

impl Size {
    /// Scale factor between font "Size" type and point size
    #[inline]
    pub fn factor() -> f32 {
        2.0
    }

    /// Create a new `Size` from a f32 size in points
    pub fn new(size: f32) -> Size {
        Size((size * Size::factor()) as i16)
    }

    /// Get the f32 size in points
    pub fn as_f32_pts(self) -> f32 {
        f32::from(self.0) / Size::factor()
    }
}

impl Add for Size {
    type Output = Size;

    fn add(self, other: Size) -> Size {
        Size(self.0.saturating_add(other.0))
    }
}

#[derive(Debug, Clone)]
pub struct Font {
    // Font size in points
    pub size: Size,
}

impl Font {
    /// Get the font size in points
    #[inline]
    pub fn size(&self) -> Size {
        self.size
    }
}

impl Default for Font {
    fn default() -> Font {
        Font {
            size: Size::new(11.0),
        }
    }
}
