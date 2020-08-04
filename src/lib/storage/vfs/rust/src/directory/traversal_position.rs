// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::default::Default;

/// Seek position inside a directory.  The precise meaning of the Name and Index values are entirely
/// up to an implementation; it could indicate the next entry to be returned or the last entry
/// returned; the type should be considered to be opaque to the client.  There are some
/// implementations that return entries in alphabetical order, but they are not required to do so.
/// The Start value indicates the first entry should be returned and the End value indicates no more
/// entries should be returned.
#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub enum TraversalPosition {
    /// The first entry in the directory.
    Start,
    /// A name of an entry.
    Name(String),
    /// The index of an entry.
    Index(u64),
    /// The whole listing was traversed.  There is nothing else to return.
    End,
}

/// The default value specifies a traversal that is positioned on the very first entry.
impl Default for TraversalPosition {
    fn default() -> Self {
        TraversalPosition::Start
    }
}
