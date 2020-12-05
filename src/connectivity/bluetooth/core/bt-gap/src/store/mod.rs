// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// These modules provide access to the component storage.
pub mod stash;

/// A test-only in-memory implementation of the bt-gap store
#[cfg(test)]
mod in_memory;

mod keys;
mod serde;
