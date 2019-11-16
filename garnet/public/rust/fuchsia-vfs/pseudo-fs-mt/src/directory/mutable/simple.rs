// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is an implementation of a mutable "simple" pseudo directories.  Use [`simple()`] to
//! construct actual instances.  See [`Simple`] for details.

#[cfg(test)]
mod tests;

use crate::directory::{
    mutable::connection::MutableConnection, simple, traversal_position::AlphabeticalTraversal,
};

use std::sync::Arc;

pub type Connection = MutableConnection<AlphabeticalTraversal>;
pub type Simple = simple::Simple<Connection>;

/// Creates a mutable empty "simple" directory.  This directory holds a "static" set of entries,
/// allowing the server to add or remove entries via the [`add_entry`] and [`remove_entry`]
/// methods.  These directories content can be modified by the client.  It uses
/// [`directory::mutable::connection::MutableConnection`] type for the connection objects.
pub fn simple() -> Arc<Simple> {
    Simple::new(true)
}
