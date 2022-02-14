// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

//! The [Hanging Get pattern](https://fuchsia.dev/fuchsia-src/development/api/fidl#hanging-get)
//! can be used when pull-based flow control is needed on a protocol.

/// This module provides generalized rust implementations of the hanging get pattern for client side use.
pub mod client;

/// This module provides generalized rust implementations of the hanging get pattern for server side
/// use. See `crate::hanging_get::server::HangingGetBroker` for documentation on how to use the
/// server side API.
pub mod server;

/// This module provides error types that may be used by the async hanging-get server.
pub mod error;

#[allow(missing_docs)]
pub mod test_util;
