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

use std::time::Duration;

use ansi::CursorStyle;
use color::Colors;
use font::Font;

#[derive(Clone, Debug)]
pub struct Selection {
    pub semantic_escape_chars: String,
}

impl Default for Selection {
    fn default() -> Selection {
        Selection {
            semantic_escape_chars: String::from(",│`|:\"' ()[]{}<>")
        }
    }
}

/// `VisualBellAnimations` are modeled after a subset of CSS transitions and Robert
/// Penner's Easing Functions.
#[derive(Clone, Copy, Debug)]
pub enum VisualBellAnimation {
    Ease,          // CSS
    EaseOut,       // CSS
    EaseOutSine,   // Penner
    EaseOutQuad,   // Penner
    EaseOutCubic,  // Penner
    EaseOutQuart,  // Penner
    EaseOutQuint,  // Penner
    EaseOutExpo,   // Penner
    EaseOutCirc,   // Penner
    Linear,
}

impl Default for VisualBellAnimation {
    fn default() -> Self {
        VisualBellAnimation::EaseOutExpo
    }
}

#[derive(Debug)]
pub struct VisualBellConfig {
    /// Visual bell animation function
    animation: VisualBellAnimation,

    /// Visual bell duration in milliseconds
    duration: u16,
}

impl VisualBellConfig {
    /// Visual bell animation
    #[inline]
    pub fn animation(&self) -> VisualBellAnimation {
        self.animation
    }

    /// Visual bell duration in milliseconds
    #[inline]
    pub fn duration(&self) -> Duration {
        Duration::from_millis(u64::from(self.duration))
    }
}

impl Default for VisualBellConfig {
    fn default() -> VisualBellConfig {
        VisualBellConfig {
            animation: VisualBellAnimation::default(),
            duration: 150,
        }
    }
}

/// Top-level config type
#[derive(Debug)]
pub struct Config {
    /// Font configuration
    font: Font,

    /// Should use custom cursor colors
    custom_cursor_colors: bool,

    /// Should draw bold text with brighter colors instead of bold font
    draw_bold_text_with_bright_colors: bool,

    colors: Colors,

    selection: Selection,

    /// Visual bell configuration
    visual_bell: VisualBellConfig,

    /// Use dynamic title
    dynamic_title: bool,

    /// Style of the cursor
    cursor_style: CursorStyle,

    /// Number of spaces in one tab
    tabspaces: usize,
}

impl Config {
    /// Get list of colors
    ///
    /// The ordering returned here is expected by the terminal. Colors are simply indexed in this
    /// array for performance.
    pub fn colors(&self) -> &Colors {
        &self.colors
    }

    pub fn selection(&self) -> &Selection {
        &self.selection
    }

    pub fn tabspaces(&self) -> usize {
        self.tabspaces
    }

    #[inline]
    pub fn draw_bold_text_with_bright_colors(&self) -> bool {
        self.draw_bold_text_with_bright_colors
    }

    /// Get font config
    #[inline]
    pub fn font(&self) -> &Font {
        &self.font
    }
    /// Get visual bell config
    #[inline]
    pub fn visual_bell(&self) -> &VisualBellConfig {
        &self.visual_bell
    }

    /// show cursor as inverted
    #[inline]
    pub fn custom_cursor_colors(&self) -> bool {
        self.custom_cursor_colors
    }

    /// Style of the cursor
    #[inline]
    pub fn cursor_style(&self) -> CursorStyle {
        self.cursor_style
    }

    #[inline]
    pub fn dynamic_title(&self) -> bool {
        self.dynamic_title
    }
}

impl Default for Config {
    fn default() -> Config {
        Config {
            font: Font::default(),
            custom_cursor_colors: false,
            draw_bold_text_with_bright_colors: true,
            colors: Colors::default(),
            selection: Selection::default(),
            visual_bell: VisualBellConfig::default(),
            dynamic_title: true,
            cursor_style: CursorStyle::default(),
            tabspaces: 8
        }
    }
}

