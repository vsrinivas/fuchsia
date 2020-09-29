// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(unused_imports)]
use anyhow::{self, Context};
use fidl_fuchsia_pkg::PackageUrl;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    let svc =
        fuchsia_component::client::connect_to_service::<fidl_fuchsia_update_usb::CheckerMarker>()
            .expect("connect OK");

    svc.check(&mut PackageUrl { url: "fuchsia-pkg://fuchsia.com/update".to_owned() }, None, None)
        .await
        .expect("send check succeeds")
        .expect("update succeeds");

    eprintln!("update OK!");

    Ok(())
}
