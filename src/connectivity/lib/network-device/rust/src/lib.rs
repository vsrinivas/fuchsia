// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia netdevice client library

#![deny(missing_docs)]
pub mod client;
pub mod error;
pub mod session;

pub use client::Client;
pub use error::Error;
pub use session::{Buffer, Config, DeviceInfo, Session};
