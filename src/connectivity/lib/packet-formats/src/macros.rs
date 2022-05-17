// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Macros used in network packet parsing and serialization.

macro_rules! __create_protocol_enum_inner {
    // Create protocol enum when the Other variant is specified.
    //
    // A `From` implementation will be provided from `$repr`. The unspecified values will
    // be mapped to the Other variant.
    ($(#[$attr:meta])* ($($vis:tt)*) enum $name:ident: $repr:ty {
        $($variant:ident, $value:expr, $fmt:expr;)*
        + $delegate_name:ident($delegate_ty:ty);
        _, $other_fmt:expr;
    }) => {
        $(#[$attr])*
        $($vis)* enum $name {
            $($variant,)*
            $delegate_name($delegate_ty),
            Other($repr),
        }

        impl From<$repr> for $name {
            fn from(x: $repr) -> $name {
                use core::convert::TryFrom;
                match x {
                    $($value => $name::$variant,)*
                    x => <$delegate_ty>::try_from(x).map($name::$delegate_name).unwrap_or($name::Other(x)),
                }
            }
        }

        impl From<$delegate_ty> for $name {
            fn from(x: $delegate_ty) -> $name {
                $name::$delegate_name(x)
            }
        }

        impl Into<$repr> for $name {
            fn into(self) -> $repr {
                match self {
                    $($name::$variant => $value,)*
                    $name::$delegate_name(x) => x.into(),
                    $name::Other(x) => x,
                }
            }
        }

        impl core::fmt::Display for $name {
            fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
                write!(
                    f,
                    "{}",
                    match self {
                        $($name::$variant => $fmt,)*
                        $name::$delegate_name(x) => return write!(f, "{}", x),
                        $name::Other(x) => return write!(f, $other_fmt, x),
                    }
                )
            }
        }

        impl core::fmt::Debug for $name {
            fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
                core::fmt::Display::fmt(self, f)
            }
        }
    };

    // Create protocol enum when the Other variant is not specified.
    //
    // In this case, a `TryFrom` implementation is provided from `$repr` instead of a `From`
    // implementation.
    ($(#[$attr:meta])* ($($vis:tt)*) enum $name:ident: $repr:ty {
        $($variant:ident, $value:expr, $fmt:expr;)*
        + $delegate_name:ident($delegate_ty:ty);
    }) => {
        $(#[$attr])*
        $($vis)* enum $name {
            $($variant,)*
            $delegate_name($delegate_ty),
        }

        impl core::convert::TryFrom<$repr> for $name {
            type Error = crate::error::UnrecognizedProtocolCode<$repr>;

            fn try_from(x: $repr) -> Result<Self, Self::Error> {
                use core::convert::TryFrom;
                match x {
                    $($value => Ok($name::$variant),)*
                    x => $delegate_ty::try_from(x).map($name::$delegate_name),
                }
            }
        }

        impl Into<$repr> for $name {
            fn into(self) -> $repr {
                match self {
                    $($name::$variant => $value,)*
                    $name::$delegate_name(x) => x.into(),
                }
            }
        }

        impl core::fmt::Display for $name {
            fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
                write!(
                    f,
                    "{}",
                    match self {
                        $($name::$variant => $fmt,)*
                        $name::$delegate_name(x) => return write!(f, "{}", x),
                    }
                )
            }
        }

        impl core::fmt::Debug for $name {
            fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
                core::fmt::Display::fmt(self, f)
            }
        }
    };

    // Create protocol enum when the Other variant is specified.
    //
    // A `From` implementation will be provided from `$repr`. The unspecified values will
    // be mapped to the Other variant.
    ($(#[$attr:meta])* ($($vis:tt)*) enum $name:ident: $repr:ty {
        $($variant:ident, $value:expr, $fmt:expr;)*
        _, $other_fmt:expr;
    }) => {
        $(#[$attr])*
        $($vis)* enum $name {
            $($variant,)*
            Other($repr),
        }

        impl From<$repr> for $name {
            fn from(x: $repr) -> $name {
                match x {
                    $($value => $name::$variant,)*
                    x => $name::Other(x),
                }
            }
        }

        impl From<$name> for $repr {
            fn from(name: $name) -> $repr {
                match name {
                    $($name::$variant => $value,)*
                    $name::Other(x) => x,
                }
            }
        }

        impl core::fmt::Display for $name {
            fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
                write!(
                    f,
                    "{}",
                    match self {
                        $($name::$variant => $fmt,)*
                        $name::Other(x) => return write!(f, $other_fmt, x),
                    }
                )
            }
        }

        impl core::fmt::Debug for $name {
            fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
                core::fmt::Display::fmt(self, f)
            }
        }
    };

    // Create protocol enum when the Other variant is not specified.
    //
    // In this case, a `TryFrom` implementation is provided from `$repr` instead of a `From`
    // implementation.
    ($(#[$attr:meta])* ($($vis:tt)*) enum $name:ident: $repr:ty {
        $($variant:ident, $value:expr, $fmt:expr;)*
    }) => {
        $(#[$attr])*
        $($vis)* enum $name {
            $($variant,)*
        }

        impl core::convert::TryFrom<$repr> for $name {
            type Error = crate::error::UnrecognizedProtocolCode<$repr>;

            fn try_from(x: $repr) -> Result<Self, Self::Error> {
                match x {
                    $($value => Ok($name::$variant),)*
                    x => Err(crate::error::UnrecognizedProtocolCode(x)),
                }
            }
        }

        impl From<$name> for $repr {
            fn from(name: $name) -> $repr {
                match name {
                    $($name::$variant => $value,)*
                }
            }
        }

        impl core::fmt::Display for $name {
            fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
                write!(
                    f,
                    "{}",
                    match self {
                        $($name::$variant => $fmt,)*
                    }
                )
            }
        }

        impl core::fmt::Debug for $name {
            fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> Result<(), core::fmt::Error> {
                core::fmt::Display::fmt(self, f)
            }
        }
    };
}

/// Create an enum representing a protocol number (such as IP protocol or
/// EtherType).
///
/// `create_protocol_enum` takes an input that looks similar (although not
/// identical) to a normal enum definition:
///
/// ```rust,ignore
/// create_protocol_enum!(
///     /// IPv4 protocol number.
///     ///
///     /// The protocol numbers are specified in
///     /// https://www.iana.org/assignments/protocol-numbers/protocol-numbers.xhtml
///     #[allow(missing_docs)]
///     #[derive(Copy, Clone, Hash, Eq, PartialEq)]
///     pub enum Ipv4Proto: u8 {
///         Icmp, 1, "ICMP";
///         Igmp, 2, "IGMP";
///         Tcp, 6, "TCP";
///         Udp, 17, "UDP";
///         + Proto(IpProto);
///         _, "IPv4 protocol {}";
///     }
/// );
/// ```
///
/// Unlike a normal enum definition, this macro takes the following extra
/// information:
/// - The type of the underlying numerical representation of the protocol number
///   (`u8`, `u16`, etc)
/// - For each variant, besides the variant's name:
///   - The value of the protocol number associated with that variant
///   - A string representation used in implementations of `Display` and `Debug`
/// - A generic string representation used in the `Display` and `Debug` impls to
///   print unrecognized protocol numbers
///
/// Note that, in addition to the variants specified in the macro invocation, the
/// following special variants may be added:
/// - A variant which includes data may be added using the syntax `+
///   Variant(Type);`
/// - An `Other` variant which captures all values not given their own variants
///   may be added using the syntax `_, "format string {}"`
///
/// For a numerical type `U` (`u8`, `u16`, etc), impls of `Into<U>` are generated.
/// `From<U>` impls are generated if the `Other` variant is specified. If the
/// `Other` variant is not specified, `TryFrom<U>` will be generated instead.
macro_rules! create_protocol_enum {
    ($(#[$attr:meta])* enum $name:ident: $repr:ty {
        $($variant:ident, $value:expr, $fmt:expr;)*
        + $delegate_name:ident($delegate_ty:ty);
        _, $other_fmt:expr;
    }) => {
        // use `()` to explicitly forward the information about private items
        __create_protocol_enum_inner!($(#[$attr])* () enum $name: $repr { $($variant, $value, $fmt;)* + $delegate_name($delegate_ty); _, $other_fmt; });
    };
    ($(#[$attr:meta])* pub enum $name:ident: $repr:ty {
        $($variant:ident, $value:expr, $fmt:expr;)*
        + $delegate_name:ident($delegate_ty:ty);
        _, $other_fmt:expr;
    }) => {
        __create_protocol_enum_inner!($(#[$attr])* (pub) enum $name: $repr { $($variant, $value, $fmt;)* + $delegate_name($delegate_ty); _, $other_fmt; });
    };
    ($(#[$attr:meta])* enum $name:ident: $repr:ty {
        $($variant:ident, $value:expr, $fmt:expr;)*
        + $delegate_name:ident($delegate_ty:ty);
    }) => {
        // use `()` to explicitly forward the information about private items
        __create_protocol_enum_inner!($(#[$attr])* () enum $name: $repr { $($variant, $value, $fmt;)* + $delegate_name($delegate_ty); });
    };
    ($(#[$attr:meta])* pub enum $name:ident: $repr:ty {
        $($variant:ident, $value:expr, $fmt:expr;)*
        + $delegate_name:ident($delegate_ty:ty);
    }) => {
        __create_protocol_enum_inner!($(#[$attr])* (pub) enum $name: $repr { $($variant, $value, $fmt;)* + $delegate_name($delegate_ty); });
    };

    ($(#[$attr:meta])* enum $name:ident: $repr:ty {
        $($variant:ident, $value:expr, $fmt:expr;)*
        _, $other_fmt:expr;
    }) => {
        // use `()` to explicitly forward the information about private items
        __create_protocol_enum_inner!($(#[$attr])* () enum $name: $repr { $($variant, $value, $fmt;)* _, $other_fmt; });
    };
    ($(#[$attr:meta])* pub enum $name:ident: $repr:ty {
        $($variant:ident, $value:expr, $fmt:expr;)*
        _, $other_fmt:expr;
    }) => {
        __create_protocol_enum_inner!($(#[$attr])* (pub) enum $name: $repr { $($variant, $value, $fmt;)* _, $other_fmt; });
    };
    ($(#[$attr:meta])* enum $name:ident: $repr:ty {
        $($variant:ident, $value:expr, $fmt:expr;)*
    }) => {
        // use `()` to explicitly forward the information about private items
        __create_protocol_enum_inner!($(#[$attr])* () enum $name: $repr { $($variant, $value, $fmt;)* });
    };
    ($(#[$attr:meta])* pub enum $name:ident: $repr:ty {
        $($variant:ident, $value:expr, $fmt:expr;)*
    }) => {
        __create_protocol_enum_inner!($(#[$attr])* (pub) enum $name: $repr { $($variant, $value, $fmt;)* });
    };
}

/// Like `Result::expect`, but takes a format string and arguments which will be
/// evaluated only when panicking.
macro_rules! expect {
    ($e:expr, $fmt:literal $(, $args:expr)*) => {
        $e.unwrap_or_else(|_| panic!($fmt $(, $args)*))
    }
}

/// Declare a benchmark function.
///
/// The function will be named `$name`. If the `benchmark` feature is enabled,
/// it will be annotated with the `#[bench]` attribute, and the provided `$fn`
/// will be invoked with a `&mut test::Bencher` - in other words, a real
/// benchmark. If the `benchmark` feature is disabled, the function will be
/// annotated with the `#[test]` attribute, and the provided `$fn` will be
/// invoked with a `&mut TestBencher`, which has the effect of creating a test
/// that runs the benchmarked function for a single iteration.
///
/// Note that `$fn` doesn't have to be a named function - it can also be an
/// anonymous closure.
#[cfg(test)]
macro_rules! bench {
    ($name:ident, $fn:expr) => {
        #[cfg(feature = "benchmark")]
        #[bench]
        fn $name(b: &mut test::Bencher) {
            $fn(b);
        }

        // TODO(joshlf): Remove the `#[ignore]` once all benchmark tests pass.
        #[cfg(not(feature = "benchmark"))]
        #[test]
        fn $name() {
            $fn(&mut crate::testutil::benchmarks::TestBencher);
        }
    };
}
