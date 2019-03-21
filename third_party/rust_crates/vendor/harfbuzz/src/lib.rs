// Copyright 2018 The Servo Project Developers. See the COPYRIGHT
// file at the top-level directory of this distribution.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

//! HarfBuzz is a text shaping engine. It solves the problem of selecting
//! and positioning glyphs from a font given a Unicode string.

#![warn(missing_docs)]
#![deny(trivial_numeric_casts, unstable_features, unused_import_braces, unused_qualifications)]

pub extern crate harfbuzz_sys as sys;

mod buffer;
pub use self::buffer::Buffer;

mod direction;
pub use self::direction::Direction;

mod language;
pub use self::language::Language;

mod blob;
pub use self::blob::Blob;
