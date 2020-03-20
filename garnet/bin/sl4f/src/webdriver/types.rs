// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::Serialize;
use std::vec::Vec;

/// Result returned by `EnableDevTools` API
#[derive(Serialize)]
pub enum EnableDevToolsResult {
    Success,
}

/// Result returned by `GetDevtoolsPorts` API.
#[derive(Serialize)]
pub struct GetDevToolsPortsResult {
    /// List of open DevTools ports.
    ports: Vec<u16>,
}

impl GetDevToolsPortsResult {
    /// Create a new `GetDevtoolsPortsResult` with the provided list of ports.
    pub fn new(ports: Vec<u16>) -> GetDevToolsPortsResult {
        GetDevToolsPortsResult { ports }
    }
}
