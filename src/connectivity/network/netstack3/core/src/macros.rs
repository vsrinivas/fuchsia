// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Macros used in Netstack3.

macro_rules! log_unimplemented {
    ($nocrash:expr, $fmt:expr $(,$arg:expr)*) => {{

        #[cfg(feature = "crash_on_unimplemented")]
        unimplemented!($fmt, $($arg),*);

        #[cfg(not(feature = "crash_on_unimplemented"))]
        // Clippy doesn't like blocks explicitly returning ().
        #[allow(clippy::unused_unit)]
        {
            // log doesn't play well with the new macro system; it expects all
            // of its macros to be in scope
            use ::log::*;
            trace!(concat!("Unimplemented: ", $fmt), $($arg),*);
            $nocrash
        }
    }};

    ($nocrash:expr, $fmt:expr $(,$arg:expr)*,) =>{
        log_unimplemented!($nocrash, $fmt $(,$arg)*)
    };
}

macro_rules! increment_counter {
    ($ctx:ident, $key:expr) => {
        #[cfg(test)]
        $ctx.state_mut().test_counters.increment($key);
    };
}

macro_rules! __create_protocol_enum_inner {
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

        impl Into<$repr> for $name {
            fn into(self) -> $repr {
                match self {
                    $($name::$variant => $value,)*
                    $name::Other(x) => x,
                }
            }
        }

        impl std::fmt::Display for $name {
            fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
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

        impl std::fmt::Debug for $name {
            fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
                std::fmt::Display::fmt(self, f)
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
///     /// An IP protocol or next header number.
///     ///
///     /// For IPv4, this is the protocol number. For IPv6, this is the next header
///     /// number.
///     #[derive(Copy, Clone, Hash, Eq, PartialEq)]
///     pub(crate) enum IpProtocol: u8 {
///         Icmp, 1, "ICMP";
///         Igmp, 2, "IGMP";
///         Tcp, 6, "TCP";
///         Udp, 17, "UDP";
///         Icmpv6, 58, "ICMPv6";
///         _, "IP protocol {}";
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
/// Note that, in addition to the variants specified in the macro invocation, an
/// extra `Other` variant is added to capture all values not given their own
/// variants.
///
/// For a numerical type `U` (`u8`, `u16`, etc), impls of `From<U>` and
/// `Into<U>` are generated.
macro_rules! create_protocol_enum {
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
    ($(#[$attr:meta])* pub ($($vis:tt)+) enum $name:ident: $repr:ty {
        $($variant:ident, $value:expr, $fmt:expr;)*
        _, $other_fmt:expr;
    }) => {
        __create_protocol_enum_inner!($(#[$attr])* (pub ($($vis)+)) enum $name: $repr { $($variant, $value, $fmt;)* _, $other_fmt; });
    };
    () => ()
}
