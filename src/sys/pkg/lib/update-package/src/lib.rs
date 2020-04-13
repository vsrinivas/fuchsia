// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Typesafe wrappers around an "update" package.

use fidl_fuchsia_io::DirectoryProxy;

/// An open handle to an "update" package.
#[derive(Debug)]
pub struct UpdatePackage {
    proxy: DirectoryProxy,
}

impl UpdatePackage {
    /// Creates a new [`UpdatePackage`] with the given proxy.
    pub fn new(proxy: DirectoryProxy) -> Self {
        Self { proxy }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_io::DirectoryMarker};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn lifecycle() {
        let (proxy, _server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        UpdatePackage::new(proxy);
    }
}
