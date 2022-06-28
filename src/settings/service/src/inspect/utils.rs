// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Enums defined specifically for inspect.
pub mod enums;

/// A queue of fixed length that holds inspect-writeable items that is
/// inspect-writeable itself.
pub mod inspect_queue;

/// A map from String to inspect-writeable item that can be written to inspect.
pub mod inspect_writable_map;

/// A map that controls an inspect node and writes inserted items to the node automatically.
pub mod managed_inspect_map;
