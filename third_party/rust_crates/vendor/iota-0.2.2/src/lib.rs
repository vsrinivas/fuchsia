//! [![github]](https://github.com/dtolnay/iota)&ensp;[![crates-io]](https://crates.io/crates/iota)&ensp;[![docs-rs]](https://docs.rs/iota)
//!
//! [github]: https://img.shields.io/badge/github-8da0cb?style=for-the-badge&labelColor=555555&logo=github
//! [crates-io]: https://img.shields.io/badge/crates.io-fc8d62?style=for-the-badge&labelColor=555555&logo=rust
//! [docs-rs]: https://img.shields.io/badge/docs.rs-66c2a5?style=for-the-badge&labelColor=555555&logoColor=white&logo=data:image/svg+xml;base64,PHN2ZyByb2xlPSJpbWciIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgdmlld0JveD0iMCAwIDUxMiA1MTIiPjxwYXRoIGZpbGw9IiNmNWY1ZjUiIGQ9Ik00ODguNiAyNTAuMkwzOTIgMjE0VjEwNS41YzAtMTUtOS4zLTI4LjQtMjMuNC0zMy43bC0xMDAtMzcuNWMtOC4xLTMuMS0xNy4xLTMuMS0yNS4zIDBsLTEwMCAzNy41Yy0xNC4xIDUuMy0yMy40IDE4LjctMjMuNCAzMy43VjIxNGwtOTYuNiAzNi4yQzkuMyAyNTUuNSAwIDI2OC45IDAgMjgzLjlWMzk0YzAgMTMuNiA3LjcgMjYuMSAxOS45IDMyLjJsMTAwIDUwYzEwLjEgNS4xIDIyLjEgNS4xIDMyLjIgMGwxMDMuOS01MiAxMDMuOSA1MmMxMC4xIDUuMSAyMi4xIDUuMSAzMi4yIDBsMTAwLTUwYzEyLjItNi4xIDE5LjktMTguNiAxOS45LTMyLjJWMjgzLjljMC0xNS05LjMtMjguNC0yMy40LTMzLjd6TTM1OCAyMTQuOGwtODUgMzEuOXYtNjguMmw4NS0zN3Y3My4zek0xNTQgMTA0LjFsMTAyLTM4LjIgMTAyIDM4LjJ2LjZsLTEwMiA0MS40LTEwMi00MS40di0uNnptODQgMjkxLjFsLTg1IDQyLjV2LTc5LjFsODUtMzguOHY3NS40em0wLTExMmwtMTAyIDQxLjQtMTAyLTQxLjR2LS42bDEwMi0zOC4yIDEwMiAzOC4ydi42em0yNDAgMTEybC04NSA0Mi41di03OS4xbDg1LTM4Ljh2NzUuNHptMC0xMTJsLTEwMiA0MS40LTEwMi00MS40di0uNmwxMDItMzguMiAxMDIgMzguMnYuNnoiPjwvcGF0aD48L3N2Zz4K
//!
//! <br>
//!
//! The `iota!` macro constructs a set of related constants.
//!
//! ```
//! use iota::iota;
//!
//! iota! {
//!     const A: u8 = 1 << iota;
//!         , B
//!         , C
//!         , D
//! }
//!
//! fn main() {
//!     assert_eq!(A, 1);
//!     assert_eq!(B, 2);
//!     assert_eq!(C, 4);
//!     assert_eq!(D, 8);
//! }
//! ```
//!
//! Within an `iota!` block, the `iota` variable is an untyped integer constant
//! whose value begins at 0 and increments by 1 for every constant declared in
//! the block.
//!
//! ```
//! use iota::iota;
//!
//! iota! {
//!     const A: u8 = 1 << iota;
//!         , B
//!
//!     const C: i32 = -1; // iota is not used but still incremented
//!
//!     pub const D: u8 = iota * 2;
//!         , E
//!         , F
//! }
//!
//! // `iota` begins again from 0 in this block
//! iota! {
//!     const G: usize = 1 << (iota + 10);
//!         , H
//! }
//!
//! fn main() {
//!     assert_eq!(A, 1 << 0);
//!     assert_eq!(B, 1 << 1);
//!
//!     assert_eq!(C, -1);
//!
//!     assert_eq!(D, 3 * 2);
//!     assert_eq!(E, 4 * 2);
//!     assert_eq!(F, 5 * 2);
//!
//!     assert_eq!(G, 1 << (0 + 10));
//!     assert_eq!(H, 1 << (1 + 10));
//! }
//! ```

/// Please refer to the crate-level documentation.
#[macro_export]
macro_rules! iota {
    (const $n:ident : $t:ty = $($rest:tt)+) => {
        $crate::__iota_dup!((0) const $n : $t = $($rest)+);
    };

    (pub const $n:ident : $t:ty = $($rest:tt)+) => {
        $crate::__iota_dup!((0) pub const $n : $t = $($rest)+);
    };
}

// Duplicate the input tokens so we can match on one set and trigger errors on
// the other set.
#[macro_export]
#[doc(hidden)]
macro_rules! __iota_dup {
    (($v:expr)) => {};

    (($v:expr) const $n:ident : $t:ty = $($rest:tt)+) => {
        $crate::__iota_impl!(($v) () () const $n : $t = ($($rest)+) ($($rest)+));
    };

    (($v:expr) pub const $n:ident : $t:ty = $($rest:tt)+) => {
        $crate::__iota_impl!(($v) () (pub) const $n : $t = ($($rest)+) ($($rest)+));
    };
}

#[macro_export]
#[doc(hidden)]
macro_rules! __iota_impl {
    // ERROR: Premature semicolon.
    //
    //    const A: u8 = ;
    (($v:expr) () $vis:tt const $n:ident : $t:ty = (; $($x:tt)*) ($semi:tt $($y:tt)*)) => {
        // "no rules expected the token `;`"
        $crate::__iota_impl!($semi);
    };

    // ERROR: Unexpected const, probably due to a missing semicolon.
    //
    //    const A: u8 = 1 << iota
    //    const B: u8 = 0;
    (($v:expr) ($($seen:tt)*) $vis:tt const $n:ident : $t:ty = (const $($x:tt)*) ($cons:tt $($y:tt)*)) => {
        // "no rules expected the token `const`"
        $crate::__iota_impl!($cons);
    };

    // ERROR: Missing final semicolon.
    //
    //    const A: u8 = 1 << iota
    (($v:expr) ($($seen:tt)*) $vis:tt const $n:ident : $t:ty = () $y:tt) => {
        // "unexpected end of macro invocation"
        $crate::__iota_impl!();
    };

    // OK: Emit a const and reuse the same expression for the next one.
    //
    //    const A: u8 = 1 << iota;
    //        , B
    (($v:expr) ($($seen:tt)+) ($($vis:tt)*) const $n:ident : $t:ty = (; , $i:ident $($rest:tt)*) $y:tt) => {
        $($vis)* const $n : $t = $crate::__iota_replace!(($v) (()) $($seen)+);
        $crate::__iota_impl!(($v + 1) ($($seen)+) ($($vis)*) const $i : $t = (; $($rest)*) (; $($rest)*));
    };

    // OK: Emit a const and use a different expression for the next one, if any.
    (($v:expr) ($($seen:tt)+) ($($vis:tt)*) const $n:ident : $t:ty = (; $($rest:tt)*) $y:tt) => {
        $($vis)* const $n : $t = $crate::__iota_replace!(($v) (()) $($seen)+);
        $crate::__iota_dup!(($v + 1) $($rest)*);
    };

    // OK: Munch a token into the expression for the current const.
    (($v:expr) ($($seen:tt)*) $vis:tt const $n:ident : $t:ty = ($first:tt $($rest:tt)*) $y:tt) => {
        $crate::__iota_impl!(($v) ($($seen)* $first) $vis const $n : $t = ($($rest)*) ($($rest)*));
    };

    // DONE.
    (($v:expr) ()) => {};
}

#[macro_export]
#[doc(hidden)]
macro_rules! __iota_replace {
    // Open parenthesis.
    (($v:expr) ($($stack:tt)*) ($($first:tt)*) $($rest:tt)*) => {
        $crate::__iota_replace!(($v) (() $($stack)*) $($first)* __iota_close_paren $($rest)*)
    };

    // Open square bracket.
    (($v:expr) ($($stack:tt)*) [$($first:tt)*] $($rest:tt)*) => {
        $crate::__iota_replace!(($v) (() $($stack)*) $($first)* __iota_close_bracket $($rest)*)
    };

    // Open curly brace.
    (($v:expr) ($($stack:tt)*) {$($first:tt)*} $($rest:tt)*) => {
        $crate::__iota_replace!(($v) (() $($stack)*) $($first)* __iota_close_brace $($rest)*)
    };

    // Close parenthesis.
    (($v:expr) (($($close:tt)*) ($($top:tt)*) $($stack:tt)*) __iota_close_paren $($rest:tt)*) => {
        $crate::__iota_replace!(($v) (($($top)* ($($close)*)) $($stack)*) $($rest)*)
    };

    // Close square bracket.
    (($v:expr) (($($close:tt)*) ($($top:tt)*) $($stack:tt)*) __iota_close_bracket $($rest:tt)*) => {
        $crate::__iota_replace!(($v) (($($top)* [$($close)*]) $($stack)*) $($rest)*)
    };

    // Close curly brace.
    (($v:expr) (($($close:tt)*) ($($top:tt)*) $($stack:tt)*) __iota_close_brace $($rest:tt)*) => {
        $crate::__iota_replace!(($v) (($($top)* {$($close)*}) $($stack)*) $($rest)*)
    };

    // Replace `iota` token with the intended expression.
    (($v:expr) (($($top:tt)*) $($stack:tt)*) iota $($rest:tt)*) => {
        $crate::__iota_replace!(($v) (($($top)* $v) $($stack)*) $($rest)*)
    };

    // Munch a token that is not `iota`.
    (($v:expr) (($($top:tt)*) $($stack:tt)*) $first:tt $($rest:tt)*) => {
        $crate::__iota_replace!(($v) (($($top)* $first) $($stack)*) $($rest)*)
    };

    // Done.
    (($v:expr) (($($top:tt)+))) => {
        $($top)+
    };
}
