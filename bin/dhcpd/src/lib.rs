// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Avoid introducing dependencies on fuchsia-* crates. By avoiding
/// use of those crates, it will be possible to support host-side
/// test targets once we have build support (TC-161).

extern crate byteorder;
extern crate bytes;

pub mod protocol;
pub mod server;

