// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common data structures.

mod id_map;
mod id_map_collection;
pub(crate) mod ref_counted_hash_map;
#[cfg(test)] // TODO(https://fxbug.dev/96320): remove [cfg(test)]
pub(crate) mod socketmap;
pub(crate) mod token_bucket;

pub use id_map::IdMap;
pub use id_map_collection::{Entry, IdMapCollection, IdMapCollectionKey};
