// Copyright 2017 The UNIC Project Developers.
//
// See the COPYRIGHT file at the top-level directory of this distribution.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use unic_char_property::EnumeratedCharProperty;
use unic_ucd_category::GeneralCategory;

/// Every char falls in exactly one of the major categories; with the exception of `CasedLetter`,
/// when it's also a `Letter`.
#[test]
fn test_general_category_major_groups() {
    for gc in GeneralCategory::all_values() {
        if gc.is_cased_letter() {
            assert!(
                gc.is_letter()
                    && !gc.is_mark()
                    && !gc.is_number()
                    && !gc.is_punctuation()
                    && !gc.is_symbol()
                    && !gc.is_separator()
                    && !gc.is_other(),
                "GC: `{:?}`",
                gc
            );
        } else if gc.is_letter() {
            assert!(
                !gc.is_mark()
                    && !gc.is_number()
                    && !gc.is_punctuation()
                    && !gc.is_symbol()
                    && !gc.is_separator()
                    && !gc.is_other(),
                "GC: `{:?}`",
                gc
            );
        } else if gc.is_mark() {
            assert!(
                !gc.is_number()
                    && !gc.is_punctuation()
                    && !gc.is_symbol()
                    && !gc.is_separator()
                    && !gc.is_other(),
                "GC: `{:?}`",
                gc
            );
        } else if gc.is_number() {
            assert!(
                !gc.is_punctuation() && !gc.is_symbol() && !gc.is_separator() && !gc.is_other(),
                "GC: `{:?}`",
                gc
            );
        } else if gc.is_punctuation() {
            assert!(
                !gc.is_symbol() && !gc.is_separator() && !gc.is_other(),
                "GC: `{:?}`",
                gc
            );
        } else if gc.is_symbol() {
            assert!(!gc.is_separator() && !gc.is_other(), "GC: `{:?}`", gc);
        } else if gc.is_separator() {
            assert!(!gc.is_other(), "GC: `{:?}`", gc);
        } else {
            assert!(gc.is_other(), "GC: `{:?}`", gc);
        }
    }
}
