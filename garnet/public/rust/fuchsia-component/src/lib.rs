// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Connect to or provide Fuchsia services.

#![deny(missing_docs)]

/// Creates an `&'static str` containing the URL of a Fuchsia package
/// from a string literal containng the name of a fuchsia component
/// containing only a single package.
///
/// e.g. `fuchsia_single_component_package_url!("my_server")` would
/// create `fuchsia-pkg://fuchsia.com/my_server#meta/my_server.cmx`.
#[macro_export]
macro_rules! fuchsia_single_component_package_url {
    ($component_name:expr) => {
        concat!("fuchsia-pkg://fuchsia.com/", $component_name, "#meta/", $component_name, ".cmx",)
    };
}

/// The name of the default instance of a FIDL Unified Service.
pub const DEFAULT_SERVICE_INSTANCE: &'static str = "default";

pub mod client;
pub mod server;
