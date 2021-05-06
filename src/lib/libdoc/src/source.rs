// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines a documentation source (a substring within a FIDL file).
//! Defines a location within a source.

use std::rc::Rc;

/// Defines a documentation source.
pub struct Source {
    /// Name of the file which contains the documentation.
    pub file_name: String,
    /// Position in the file (line) of the first character of the documentation.
    pub line: u32,
    /// Position in the file (column) of the first character of the documentation.
    pub column: u32,
    /// Text of the documentation.
    pub text: String,
}

impl Source {
    pub fn new(file_name: String, line: u32, column: u32, text: String) -> Source {
        Source { file_name, line, column, text }
    }
}

/// Defines the location of an item within a documentation source.
#[derive(Clone)]
pub struct Location {
    /// The source of the documentation.
    pub source: Rc<Source>,
    /// The offset of the first character of the item.
    pub start: usize,
    /// The offset of the first character following the item.
    pub end: usize,
}
