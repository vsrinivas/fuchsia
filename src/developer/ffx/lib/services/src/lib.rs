// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
mod context;
mod fake_daemon;
mod register;
mod services;

pub mod prelude;
pub use context::{Context, DaemonServiceProvider};
pub use core_service_macros::ffx_service;
pub use fake_daemon::{FakeDaemon, FakeDaemonBuilder};
pub use register::{NameToStreamHandlerMap, ServiceError, ServiceRegister};
pub use services::{FidlInstancedStreamHandler, FidlService, FidlStreamHandler, StreamHandler};
