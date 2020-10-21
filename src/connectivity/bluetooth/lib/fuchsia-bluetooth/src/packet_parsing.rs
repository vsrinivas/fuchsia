// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Generates an enum value where each variant can be converted into a constant in the given
/// raw_type. The visibility of the generated enum is limited to the crate its defined in.
/// For example:
/// decodable_enum! {
///     Color<u8, MyError, MyError::Variant> {
///        Red => 1,
///        Blue => 2,
///        Green => 3,
///     }
/// }
/// Then Color::try_from(2) returns Color::Red, and u8::from(Color::Red) returns 1.
#[macro_export]
macro_rules! decodable_enum {
    ($(#[$meta:meta])* $name:ident<$raw_type:ty,$error_type:ident,$error_path:ident> {
        $($(#[$variant_meta:meta])* $variant:ident => $val:expr),*,
    }) => {
        $(#[$meta])*
        #[derive(
            ::core::clone::Clone,
            ::core::marker::Copy,
            ::core::fmt::Debug,
            ::core::cmp::Eq,
            ::core::hash::Hash,
            ::core::cmp::PartialEq)]
        pub(crate) enum $name {
            $($(#[$variant_meta])* $variant = $val),*
        }

        $crate::tofrom_decodable_enum! {
            $name<$raw_type, $error_type, $error_path> {
                $($variant => $val),*,
            }
        }
    }
}

/// Generates an enum value where each variant can be converted into a constant in the given
/// raw_type.  For example:
/// pub_decodable_enum! {
///     Color<u8, MyError, MyError::Variant> {
///        Red => 1,
///        Blue => 2,
///        Green => 3,
///     }
/// }
/// Then Color::try_from(2) returns Color::Red, and u8::from(Color::Red) returns 1.
#[macro_export]
macro_rules! pub_decodable_enum {
    ($(#[$meta:meta])* $name:ident<$raw_type:ty,$error_type:ident,$error_path:ident> {
        $($(#[$variant_meta:meta])* $variant:ident => $val:expr),*,
    }) => {
        $(#[$meta])*
        #[derive(
            ::core::clone::Clone,
            ::core::marker::Copy,
            ::core::fmt::Debug,
            ::core::cmp::Eq,
            ::core::hash::Hash,
            ::core::cmp::PartialEq)]
        pub enum $name {
            $($(#[$variant_meta])* $variant = $val),*
        }

        $crate::tofrom_decodable_enum! {
            $name<$raw_type, $error_type, $error_path> {
                $($variant => $val),*,
            }
        }

        impl $name {
            pub const VALUES : &'static [$raw_type] = &[$($val),*,];
            pub const VARIANTS : &'static [$name] = &[$($name::$variant),*,];

            pub fn name(&self) -> &'static ::core::primitive::str {
                match self {
                    $($name::$variant => ::core::stringify!($variant)),*
                }
            }
        }
    }
}

/// A From<&$name> for $raw_type implementation and
/// TryFrom<$raw_type> for $name implementation, used by (pub_)decodable_enum
#[macro_export]
macro_rules! tofrom_decodable_enum {
    ($name:ident<$raw_type:ty, $error_type:ident, $error_path:ident> {
        $($variant:ident => $val:expr),*,
    }) => {
        impl ::core::convert::From<&$name> for $raw_type {
            fn from(v: &$name) -> $raw_type {
                match v {
                    $($name::$variant => $val),*,
                }
            }
        }

        impl ::core::convert::TryFrom<$raw_type> for $name {
            type Error = $error_type;

            fn try_from(value: $raw_type) -> ::core::result::Result<Self, $error_type> {
                match value {
                    $($val => ::core::result::Result::Ok($name::$variant)),*,
                    _ => ::core::result::Result::Err($error_type::$error_path),
                }
            }
        }
    }
}

#[cfg(test)]
#[no_implicit_prelude]
mod test {
    use ::core::{
        assert, assert_eq,
        convert::{From, TryFrom},
        option::Option::Some,
        panic,
    };
    use ::matches::assert_matches;

    #[derive(Debug, PartialEq)]
    pub(crate) enum TestError {
        OutOfRange,
    }

    decodable_enum! {
        TestEnum<u16, TestError, OutOfRange> {
            One => 1,
            Two => 2,
            Max => 65535,
        }
    }

    #[test]
    fn try_from_success() {
        let one = TestEnum::try_from(1);
        assert!(one.is_ok());
        assert_eq!(TestEnum::One, one.unwrap());
        let two = TestEnum::try_from(2);
        assert!(two.is_ok());
        assert_eq!(TestEnum::Two, two.unwrap());
        let max = TestEnum::try_from(65535);
        assert!(max.is_ok());
        assert_eq!(TestEnum::Max, max.unwrap());
    }

    #[test]
    fn try_from_error() {
        let err = TestEnum::try_from(5);
        assert_matches!(err.err(), Some(TestError::OutOfRange));
    }

    #[test]
    fn into_rawtype() {
        let raw = u16::from(&TestEnum::One);
        assert_eq!(1, raw);
        let raw = u16::from(&TestEnum::Two);
        assert_eq!(2, raw);
        let raw = u16::from(&TestEnum::Max);
        assert_eq!(65535, raw);
    }
}
