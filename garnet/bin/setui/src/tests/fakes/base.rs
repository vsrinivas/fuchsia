// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::Error;
use fuchsia_zircon as zx;

/// Trait for providing a service.
pub trait Service {
    /// Returns true if this service can process the given service name, false
    /// otherwise.
    fn can_handle_service(&self, service_name: &str) -> bool;

    /// Processes the request stream within the specified channel. Ok is returned
    /// on success, an error otherwise.
    fn process_stream(&self, service_name: &str, channel: zx::Channel) -> Result<(), Error>;
}
