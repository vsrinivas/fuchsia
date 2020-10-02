// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// RFCOMM channels used to communicate with profile clients.
mod channel;

/// The underlying frames used as units of data in RFCOMM.
mod frame;

/// RFCOMM-specific types.
mod types;

/// The RFCOMM Session that multiplexes connections over a single L2CAP channel.
mod session;

/// The RFCOMM server that processes connections.
mod server;

pub use crate::rfcomm::{server::RfcommServer, types::ServerChannel};
