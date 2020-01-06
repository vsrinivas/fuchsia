// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is the client library for interacting with the Cobalt FIDL service.

#![deny(missing_docs)]

pub mod cobalt_event_builder;
pub mod connector;
pub mod sender;

use futures::prelude::*;

pub use {
    cobalt_event_builder::CobaltEventExt,
    connector::{CobaltConnector, ConnectionType},
    sender::CobaltSender,
};

/// Helper function to connect to the cobalt FIDL service.
///
/// # Arguments
///
/// * `buffer_size` - The number of cobalt events that can be buffered before being sent to the
///   Cobalt FIDL service.
/// * `connection_type` - A `ConnectionType` object that defines how to connect to the Cobalt FIDL
///   service.
///
/// # Example
///
/// ```no_run
/// cobalt_fidl::serve(100, ConnectionType::project_id(1234));
/// ```
pub fn serve(
    buffer_size: usize,
    connection_type: ConnectionType,
) -> (CobaltSender, impl Future<Output = ()>) {
    CobaltConnector { buffer_size, ..Default::default() }.serve(connection_type)
}
