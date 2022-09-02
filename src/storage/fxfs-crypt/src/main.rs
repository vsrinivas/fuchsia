// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{stream::StreamExt, TryFutureExt},
    fxfs_crypt::{log::*, CryptService, Services},
};

#[fasync::run(10)]
async fn main() -> Result<(), Error> {
    diagnostics_log::init!();

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(Services::Crypt).add_fidl_service(Services::CryptManagement);
    fs.take_and_serve_directory_handle()?;

    let crypt = CryptService::new();

    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |request| {
        crypt.handle_request(request).unwrap_or_else(|e| error!("{}", e))
    })
    .await;

    Ok(())
}
