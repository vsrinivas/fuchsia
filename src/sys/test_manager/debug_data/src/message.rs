// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::ServerEnd;
use fidl_fuchsia_debugdata as fdebug;

/// Message indicating a request to connect to the `fuchsia.debugdata.DebugData` protocol.
pub struct DebugDataRequestMessage {
    pub test_url: String,
    pub request: ServerEnd<fdebug::DebugDataMarker>,
}
