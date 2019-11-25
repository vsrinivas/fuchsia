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
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
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
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
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

/// Implement [`TimerContext`] for one ID type in terms of an existing
/// implementation for a different ID type.
///
/// `$outer_timer_id` is an enum where one variant contains an
/// `$inner_timer_id`. `impl_timer_context!` generates an impl of
/// `TimerContext<$inner_timer_id>` for any `C: TimerContext<$outer_timer_id>`.
///
/// An impl of `Into<$outer_timer_id> for `$inner_timer_id` must exist. `$pat`
/// is a pattern of type `$outer_timer_id` that binds the `$inner_timer_id`.
/// `$bound_variable` is the name of the bound `$inner_timer_id` from the
/// pattern. For example, if `$pat` is `OuterTimerId::Inner(id)`, then
/// `$bound_variable` would be `id`. This is required for macro hygiene.
///
/// If an extra first parameter, `$bound`, is provided, then it is added as an
/// extra bound on the `C` context type.
///
/// [`TimerContext`]: crate::context::TimerContext
macro_rules! impl_timer_context {
    ($outer_timer_id:ty, $inner_timer_id:ty, $pat:pat, $bound_variable:ident) => {
        impl<C: crate::context::TimerContext<$outer_timer_id>>
            crate::context::TimerContext<$inner_timer_id> for C
        {
            impl_timer_context!(@inner $inner_timer_id, $pat, $bound_variable);
        }
    };
    ($bound:path, $outer_timer_id:ty, $inner_timer_id:ty, $pat:pat, $bound_variable:ident) => {
        impl<C: $bound + crate::context::TimerContext<$outer_timer_id>>
            crate::context::TimerContext<$inner_timer_id> for C
        {
            impl_timer_context!(@inner $inner_timer_id, $pat, $bound_variable);
        }
    };
    (@inner $inner_timer_id:ty, $pat:pat, $bound_variable:ident) => {
        fn schedule_timer_instant(
            &mut self,
            time: Self::Instant,
            id: $inner_timer_id,
        ) -> Option<Self::Instant> {
            self.schedule_timer_instant(time, id.into())
        }

        fn cancel_timer(&mut self, id: $inner_timer_id) -> Option<Self::Instant> {
            self.cancel_timer(id.into())
        }

        fn cancel_timers_with<F: FnMut(&$inner_timer_id) -> bool>(&mut self, mut f: F) {
            self.cancel_timers_with(|id| match id {
                $pat => f($bound_variable),
                #[allow(unreachable_patterns)]
                _ => false,
            })
        }

        fn scheduled_instant(&self, id: $inner_timer_id) -> Option<Self::Instant> {
            self.scheduled_instant(id.into())
        }
    };
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
