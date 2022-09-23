// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

/// A wrapper around [std::collections::VecDeque] that holds [String]. Can be wrapped in an
/// [fuchsia_inspect_derive::IValue] and written to inspect as a single property with its value
/// being a comma-separated list that's concatenation of all of the items in the VecDeque.
pub mod joinable_inspect_vecdeque;

/// A map that controls an inspect node and writes inserted items to the node automatically.
pub mod managed_inspect_map;

/// A queue that controls an inspect node and writes inserted items to the node automatically.
pub mod managed_inspect_queue;
