// Copyright 2018 The Servo Project Developers. See the COPYRIGHT
// file at the top-level directory of this distribution.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use sys;

/// Direction of text flow during layout.
///
/// This maps to the [`hb_direction_t`] from
/// [`harfbuzz-sys`]. It can be converted to
/// or from `hb_direction_t` using the [`From`]
/// and [`Into`] traits:
///
/// ```
/// # use harfbuzz::{Direction, sys};
/// assert_eq!(Direction::from(sys::HB_DIRECTION_LTR), Direction::LTR);
/// assert_eq!(sys::hb_direction_t::from(Direction::BTT), sys::HB_DIRECTION_BTT);
///
/// let hb_dir: sys::hb_direction_t = Direction::LTR.into();
/// assert_eq!(hb_dir, sys::HB_DIRECTION_LTR);
///
/// let dir: Direction = sys::HB_DIRECTION_TTB.into();
/// assert_eq!(dir, Direction::TTB);
/// ```
///
/// [`hb_direction_t`]: ../harfbuzz_sys/type.hb_direction_t.html
/// [`harfbuzz-sys`]: ../harfbuzz_sys/index.html
/// [`From`]: https://doc.rust-lang.org/std/convert/trait.From.html
/// [`Into`]: https://doc.rust-lang.org/std/convert/trait.Into.html
#[derive(Copy, Clone, Debug, PartialEq, PartialOrd)]
pub enum Direction {
    /// Initial, unset direction.
    ///
    /// This corresponds to [`HB_DIRECTION_INVALID`].
    ///
    /// [`HB_DIRECTION_INVALID`]: ../harfbuzz_sys/constant.HB_DIRECTION_INVALID.html
    Invalid,
    /// Text is set horizontally from left to right.
    ///
    /// This corresponds to [`HB_DIRECTION_LTR`].
    ///
    /// [`HB_DIRECTION_LTR`]: ../harfbuzz_sys/constant.HB_DIRECTION_LTR.html
    LTR,
    /// Text is set horizontally from right to left.
    ///
    /// This corresponds to [`HB_DIRECTION_RTL`].
    ///
    /// [`HB_DIRECTION_RTL`]: ../harfbuzz_sys/constant.HB_DIRECTION_RTL.html
    RTL,
    /// Text is set vertically from top to bottom.
    ///
    /// This corresponds to [`HB_DIRECTION_TTB`].
    ///
    /// [`HB_DIRECTION_TTB`]: ../harfbuzz_sys/constant.HB_DIRECTION_TTB.html
    TTB,
    /// Text is set vertically from bottom to top.
    ///
    /// This corresponds to [`HB_DIRECTION_BTT`].
    ///
    /// [`HB_DIRECTION_BTT`]: ../harfbuzz_sys/constant.HB_DIRECTION_BTT.html
    BTT,
}

impl From<sys::hb_direction_t> for Direction {
    fn from(s: sys::hb_direction_t) -> Self {
        match s {
            sys::HB_DIRECTION_INVALID => Direction::Invalid,
            sys::HB_DIRECTION_LTR => Direction::LTR,
            sys::HB_DIRECTION_RTL => Direction::RTL,
            sys::HB_DIRECTION_TTB => Direction::TTB,
            sys::HB_DIRECTION_BTT => Direction::BTT,
            _ => Direction::Invalid,
        }
    }
}

impl From<Direction> for sys::hb_direction_t {
    fn from(s: Direction) -> Self {
        match s {
            Direction::Invalid => sys::HB_DIRECTION_INVALID,
            Direction::LTR => sys::HB_DIRECTION_LTR,
            Direction::RTL => sys::HB_DIRECTION_RTL,
            Direction::TTB => sys::HB_DIRECTION_TTB,
            Direction::BTT => sys::HB_DIRECTION_BTT,
        }
    }
}
