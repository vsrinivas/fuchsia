// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This test component has two modes:
//!
//! 1. Default: just runs and hangs serving its out directory. This is used to test a component
//!    that doesn't expose a out/diagnostics directory.
//!
//! 2. "with-diagnostics": this mode serves an inspect tree that just contains information about
//!    the component being healthy. Serving the inspect Tree creates a `diagnostics` directory.
//!     This is used for testing a component that exposes an out/diagnostics directory.

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{component, health::Reporter},
    futures::prelude::*,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let args: Vec<String> = std::env::args().collect();
    if args.len() > 1 && args[1] == "with-diagnostics" {
        component::health().set_ok();
        component::inspector().serve_tree(&mut fs)?;
    }
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}
