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
    pub fn init() -> Result<(), failure::Error> {
        Ok(())
    }
}

/// Helper to connect to Overnet as a ServicePublisher, and then publish a single service
pub fn publish_service(
    service_name: &str,
    provider: fidl::endpoints::ClientEnd<fidl_fuchsia_overnet::ServiceProviderMarker>,
) -> Result<(), failure::Error> {
    connect_as_service_publisher()?.publish_service(service_name, provider)?;
    Ok(())
}

#[cfg(test)]
mod test {

    use super::*;
    use std::cell::RefCell;
    use std::rc::Rc;

    #[test]
    fn run_works() {
        let done = Rc::new(RefCell::new(false));
        let done_check = done.clone();
        run(async move {
            *done.borrow_mut() = true;
        });
        assert!(*done_check.borrow());
    }
}
