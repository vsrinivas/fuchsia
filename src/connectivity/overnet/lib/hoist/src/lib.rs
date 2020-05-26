// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod fuchsia;
#[cfg(not(target_os = "fuchsia"))]
pub mod logger;
mod not_fuchsia;

#[cfg(target_os = "fuchsia")]
pub use fuchsia::*;

#[cfg(not(target_os = "fuchsia"))]
pub use not_fuchsia::*;

#[cfg(target_os = "fuchsia")]
pub mod logger {
    pub fn init() -> Result<(), anyhow::Error> {
        use anyhow::Context as _;
        fuchsia_syslog::init_with_tags(&["overnet_hoist"]).context("initialize logging")?;
        Ok(())
    }
}

/// Helper to connect to Overnet as a ServicePublisher, and then publish a single service
pub fn publish_service(
    service_name: &str,
    provider: fidl::endpoints::ClientEnd<fidl_fuchsia_overnet::ServiceProviderMarker>,
) -> Result<(), anyhow::Error> {
    connect_as_service_publisher()?.publish_service(service_name, provider)?;
    Ok(())
}

#[cfg(test)]
mod test {

    use super::*;
    use parking_lot::Mutex;
    use std::sync::Arc;

    #[test]
    fn run_works() {
        let done = Arc::new(Mutex::new(false));
        let done_check = done.clone();
        run(async move {
            *done.lock() = true;
        });
        assert!(*done_check.lock());
    }
}
