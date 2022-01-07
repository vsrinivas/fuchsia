// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Protocols library for use with the FFX Daemon.
//!
//! For most relevant and up-to-date documentation, see the [protocols] mod
//! itself.

mod context;
mod protocols;
mod register;

pub mod prelude;
pub mod testing;
pub use context::{Context, DaemonProtocolProvider};
pub use core_protocol_macros::ffx_protocol;
pub use protocols::{FidlInstancedStreamHandler, FidlProtocol, FidlStreamHandler, StreamHandler};
pub use register::{NameToStreamHandlerMap, ProtocolError, ProtocolRegister};
