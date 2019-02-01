use std::ops::{Index, IndexMut};
use std::fmt;

use ansi;
use rgb::Rgb;

#[derive(Debug)]
pub struct Colors {
    pub primary: PrimaryColors,
    pub cursor: CursorColors,
    pub normal: AnsiColors,
    pub bright: AnsiColors,
    pub dim: Option<AnsiColors>,
}

#[derive(Debug)]
pub struct CursorColors {
    pub text: Rgb,
    pub cursor: Rgb,
}

impl Default for CursorColors {
    fn default() -> Self {
        CursorColors {
            text: Rgb { r: 0, g: 0, b: 0 },
            cursor: Rgb { r: 0xff, g: 0xff, b: 0xff },
        }
    }
}

#[derive(Debug)]
pub struct PrimaryColors {
    pub background: Rgb,
    pub foreground: Rgb,
}

impl Default for PrimaryColors {
    fn default() -> Self {
        PrimaryColors {
            background: Rgb { r: 0, g: 0, b: 0 },
            foreground: Rgb { r: 0xea, g: 0xea, b: 0xea },
        }
    }
}

impl Default for Colors {
    fn default() -> Colors {
        Colors {
            primary: PrimaryColors::default(),
            cursor: CursorColors::default(),
            normal: AnsiColors {
                black: Rgb {r: 0x00, g: 0x00, b: 0x00},
                red: Rgb {r: 0xd5, g: 0x4e, b: 0x53},
                green: Rgb {r: 0xb9, g: 0xca, b: 0x4a},
                yellow: Rgb {r: 0xe6, g: 0xc5, b: 0x47},
                blue: Rgb {r: 0x7a, g: 0xa6, b: 0xda},
                magenta: Rgb {r: 0xc3, g: 0x97, b: 0xd8},
                cyan: Rgb {r: 0x70, g: 0xc0, b: 0xba},
                white: Rgb {r: 0xea, g: 0xea, b: 0xea},
            },
            bright: AnsiColors {
                black: Rgb {r: 0x66, g: 0x66, b: 0x66},
                red: Rgb {r: 0xff, g: 0x33, b: 0x34},
                green: Rgb {r: 0x9e, g: 0xc4, b: 0x00},
                yellow: Rgb {r: 0xe7, g: 0xc5, b: 0x47},
                blue: Rgb {r: 0x7a, g: 0xa6, b: 0xda},
                magenta: Rgb {r: 0xb7, g: 0x7e, b: 0xe0},
                cyan: Rgb {r: 0x54, g: 0xce, b: 0xd6},
                white: Rgb {r: 0xff, g: 0xff, b: 0xff},
            },
            dim: None,
        }
    }
}

/// The 8-colors sections of config
#[derive(Debug)]
pub struct AnsiColors {
    pub black: Rgb,
    pub red: Rgb,
    pub green: Rgb,
    pub yellow: Rgb,
    pub blue: Rgb,
    pub magenta: Rgb,
    pub cyan: Rgb,
    pub white: Rgb,
}

pub const COUNT: usize = 268;

/// List of indexed colors
///
/// The first 16 entries are the standard ansi named colors. Items 16..232 are
/// the color cube.  Items 233..256 are the grayscale ramp. Item 256 is
/// the configured foreground color, item 257 is the configured background
/// color, item 258 is the cursor foreground color, item 259 is the cursor
/// background color. Following that are 8 positions for dim colors.
#[derive(Copy, Clone)]
pub struct List([Rgb; COUNT]);

impl<'a> From<&'a Colors> for List {
    fn from(colors: &Colors) -> List {
        // Type inference fails without this annotation
        let mut list: List = unsafe { ::std::mem::uninitialized() };

        list.fill_named(colors);
        list.fill_cube();
        list.fill_gray_ramp();

        list
    }
}

impl Default for List {
    fn default() -> List {
        List::from(&Colors::default())
    }
}

impl List {
    pub fn fill_named(&mut self, colors: &Colors) {
        // Normals
        self[ansi::NamedColor::Black]   = colors.normal.black;
        self[ansi::NamedColor::Red]     = colors.normal.red;
        self[ansi::NamedColor::Green]   = colors.normal.green;
        self[ansi::NamedColor::Yellow]  = colors.normal.yellow;
        self[ansi::NamedColor::Blue]    = colors.normal.blue;
        self[ansi::NamedColor::Magenta] = colors.normal.magenta;
        self[ansi::NamedColor::Cyan]    = colors.normal.cyan;
        self[ansi::NamedColor::White]   = colors.normal.white;

        // Brights
        self[ansi::NamedColor::BrightBlack]   = colors.bright.black;
        self[ansi::NamedColor::BrightRed]     = colors.bright.red;
        self[ansi::NamedColor::BrightGreen]   = colors.bright.green;
        self[ansi::NamedColor::BrightYellow]  = colors.bright.yellow;
        self[ansi::NamedColor::BrightBlue]    = colors.bright.blue;
        self[ansi::NamedColor::BrightMagenta] = colors.bright.magenta;
        self[ansi::NamedColor::BrightCyan]    = colors.bright.cyan;
        self[ansi::NamedColor::BrightWhite]   = colors.bright.white;

        // Foreground and background
        self[ansi::NamedColor::Foreground] = colors.primary.foreground;
        self[ansi::NamedColor::Background] = colors.primary.background;

        // Foreground and background for custom cursor colors
        self[ansi::NamedColor::CursorText] = colors.cursor.text;
        self[ansi::NamedColor::Cursor]     = colors.cursor.cursor;

        // Dims
        match colors.dim {
            Some(ref dim) => {
                trace!("Using config-provided dim colors");
                self[ansi::NamedColor::DimBlack]   = dim.black;
                self[ansi::NamedColor::DimRed]     = dim.red;
                self[ansi::NamedColor::DimGreen]   = dim.green;
                self[ansi::NamedColor::DimYellow]  = dim.yellow;
                self[ansi::NamedColor::DimBlue]    = dim.blue;
                self[ansi::NamedColor::DimMagenta] = dim.magenta;
                self[ansi::NamedColor::DimCyan]    = dim.cyan;
                self[ansi::NamedColor::DimWhite]   = dim.white;
            }
            None => {
                trace!("Deriving dim colors from normal colors");
                self[ansi::NamedColor::DimBlack]   = colors.normal.black   * 0.66;
                self[ansi::NamedColor::DimRed]     = colors.normal.red     * 0.66;
                self[ansi::NamedColor::DimGreen]   = colors.normal.green   * 0.66;
                self[ansi::NamedColor::DimYellow]  = colors.normal.yellow  * 0.66;
                self[ansi::NamedColor::DimBlue]    = colors.normal.blue    * 0.66;
                self[ansi::NamedColor::DimMagenta] = colors.normal.magenta * 0.66;
                self[ansi::NamedColor::DimCyan]    = colors.normal.cyan    * 0.66;
                self[ansi::NamedColor::DimWhite]   = colors.normal.white   * 0.66;
            }
        }
    }

    fn fill_cube(&mut self) {
        let mut index: usize = 16;
        // Build colors
        for r in 0..6 {
            for g in 0..6 {
                for b in 0..6 {
                    self[index] = Rgb { r: if r == 0 { 0 } else { r * 40 + 55 },
                        b: if b == 0 { 0 } else { b * 40 + 55 },
                        g: if g == 0 { 0 } else { g * 40 + 55 },
                    };
                    index += 1;
                }
            }
        }

        debug_assert!(index == 232);
    }

    fn fill_gray_ramp(&mut self) {
        let mut index: usize = 232;

        for i in 0..24 {
            let value = i * 10 + 8;
            self[index] = Rgb {
                r: value,
                g: value,
                b: value
            };
            index += 1;
        }

        debug_assert!(index == 256);
    }
}

impl fmt::Debug for List {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str("List[..]")
    }
}

impl Index<ansi::NamedColor> for List {
    type Output = Rgb;

    #[inline]
    fn index(&self, idx: ansi::NamedColor) -> &Self::Output {
        &self.0[idx as usize]
    }
}

impl IndexMut<ansi::NamedColor> for List {
    #[inline]
    fn index_mut(&mut self, idx: ansi::NamedColor) -> &mut Self::Output {
        &mut self.0[idx as usize]
    }
}

impl Index<usize> for List {
    type Output = Rgb;

    #[inline]
    fn index(&self, idx: usize) -> &Self::Output {
        &self.0[idx]
    }
}

impl IndexMut<usize> for List {
    #[inline]
    fn index_mut(&mut self, idx: usize) -> &mut Self::Output {
        &mut self.0[idx]
    }
}

impl Index<u8> for List {
    type Output = Rgb;

    #[inline]
    fn index(&self, idx: u8) -> &Self::Output {
        &self.0[idx as usize]
    }
}

impl IndexMut<u8> for List {
    #[inline]
    fn index_mut(&mut self, idx: u8) -> &mut Self::Output {
        &mut self.0[idx as usize]
    }
}
