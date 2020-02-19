// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::default::Default;

/// Seek position inside a directory.  This traversal expect the directory to store entries in
/// alphabetical order, a traversal position is then a name of the entry that should be returned
/// next.  Dot is a special entry that is considered to sort before all the other entries.
#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub enum AlphabeticalTraversal {
    /// Dot, or "." is considered to be the first entry.
    Dot,
    /// Name of the entry that should be returned next.
    Name(String),
    /// The whole listing was traversed.  There is nothing else to return.
    End,
}

/// The default value specifies a traversal that is positioned before the very first entry.
impl Default for AlphabeticalTraversal {
    fn default() -> Self {
        AlphabeticalTraversal::Dot
    }
}
