// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! The [Hanging Get
//! pattern](https://fuchsia.dev/fuchsia-src/development/api/fidl#delay_responses_using_hanging_gets)
//! can be used when pull-based flow control is needed on a protocol.
//! This module provides generalized rust implementations of the hanging get pattern for server and
//! client side use.
//! See `crate::hanging_get::server::HangingGetBroker` for documentation on how to use the server
//! side API.

pub mod error;
pub mod server;
#[cfg(test)]
pub(crate) mod test_util;
