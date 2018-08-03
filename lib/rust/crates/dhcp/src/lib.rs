// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]
#![allow(unused)] // TODO(atait): Remove once there are non-test clients

/// Avoid introducing dependencies on fuchsia-* crates. By avoiding
/// use of those crates, it will be possible to support host-side
/// test targets once we have build support (TC-161).

extern crate byteorder;
extern crate bytes;
#[macro_use]
extern crate failure;
extern crate fuchsia_zircon as zx;
extern crate serde;
#[macro_use]
extern crate serde_derive;
extern crate serde_json;

pub mod configuration;
pub mod protocol;
pub mod server;

