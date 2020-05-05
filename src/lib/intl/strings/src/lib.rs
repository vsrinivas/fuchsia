// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Internationalization and localization tools for Fuchsia
//!
//! This crate contains a library which generates message IDs from the Android-formatted
//! [strings.xml resource file][strings-xml], as a set of FIDL constants.  Complete support is not
//! a specific goal, rather the generator will be amended to include more features as more features
//! are needed.
//!
//! [strings-xml]: https://developer.android.com/guide/topics/resources/string-resource

/// Like [eprintln!], but only if verbosity ($v) is true.
///
/// Example:
///
/// ```ignore
/// veprintln!(true, "this will be logged only if first param is true: {}", true);
/// ```
#[macro_export]
macro_rules! veprintln{
    ($v:expr, $($arg:tt)* ) => {{
        if $v {
            eprintln!($($arg)*);
        }
    }}
}

pub mod json;
pub mod message_ids;
pub mod parser;
