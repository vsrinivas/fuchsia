// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Error, Result},
    fuchsia_component::server::ServiceFs,
    futures::StreamExt,
    tracing::info,
};

/// Note to contributors: This component is test-only, so it should panic liberally. Loud crashes
/// are much easier to debug than silent failures. Please use `expect()` and `panic!` where
/// applicable.
#[fuchsia::main(logging_tags = ["ui_puppet"])]
async fn main() -> Result<(), Error> {
    info!("starting ui puppet component");

    let mut fs = ServiceFs::new_local();

    fs.dir("svc").add_fidl_service(std::convert::identity);
    fs.take_and_serve_directory_handle()?;
    fs.for_each_concurrent(None, ui_puppet_lib::run_puppet_factory).await;

    Ok(())
}
