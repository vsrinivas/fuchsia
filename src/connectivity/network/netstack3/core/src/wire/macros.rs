// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Create an enum for a network packet field.
///
/// Create a `repr(u8)` enum with a `from_u8(u8) -> Option<Self>` constructor
/// that implements `Into<u8>`.
macro_rules! create_net_enum {
    ($(#[$attr:meta])* $vis:vis $t:ident, $($val:ident: $const:ident = $value:expr,)*) => {
      create_net_enum!($(#[$attr])* $vis $t, $($val: $const = $value),*);
    };

    ($(#[$attr:meta])* $vis:vis $t:ident, $($val:ident: $const:ident = $value:expr),*) => {
        $(#[$attr])*
        #[derive(Debug, PartialEq, Copy, Clone)]
        #[repr(u8)]
        $vis enum $t {
            $($val = $t::$const),*
        }

        impl $t {
            $(const $const: u8 = $value;)*

            fn from_u8(u: u8) -> Option<$t> {
                match u {
                $($t::$const => Some($t::$val)),*,
                _ => None,
                }
            }
        }

        impl From<$t> for u8 {
            fn from(v : $t) -> u8 {
                v as u8
            }
        }

        impl ::core::convert::TryFrom<u8> for $t {
            type Error = ();

            fn try_from(value: u8) -> Result<$t, ()> {
                $t::from_u8(value).ok_or(())
            }
        }
    };
}
