// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The underlying frames used as units of data in RFCOMM.
mod frame;

/// Structures to support inspection.
mod inspect;

/// The RFCOMM server that manages connections.
mod server;

/// The RFCOMM Session that multiplexes connections over a single L2CAP channel.
mod session;

/// RFCOMM-specific types.
mod types;

/// Common helpers used in unit tests.
#[cfg(test)]
mod test_util;

pub use crate::rfcomm::{server::RfcommServer, types::ServerChannel};
