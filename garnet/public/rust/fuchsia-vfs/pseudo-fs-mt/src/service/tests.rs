// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for the service endpoint.

// Make it easier for the nested modules to import the `endpoint` and `host` constructor.
use super::{endpoint, host};

mod direct_connection;
mod node_reference;
