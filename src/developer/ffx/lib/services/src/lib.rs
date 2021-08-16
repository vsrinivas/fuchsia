// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Services library for use with the FFX Daemon.
//!
//! For most relevant and up-to-date documentation, see the [services] mod
//! itself.

mod context;
mod register;
mod services;

pub mod prelude;
pub mod testing;
pub use context::{Context, DaemonServiceProvider};
pub use core_service_macros::ffx_service;
pub use register::{NameToStreamHandlerMap, ServiceError, ServiceRegister};
pub use services::{FidlInstancedStreamHandler, FidlService, FidlStreamHandler, StreamHandler};
