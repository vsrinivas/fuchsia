// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fidl_examples_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::new_service_connector,
};

const TEST_STRING: &str = "test string";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let req = new_service_connector::<fecho::EchoMarker>().context("error probing Echo service")?;
    if !req.exists().await.context("error checking for service existence")? {
        return Err(anyhow::anyhow!("Echo service does not exist"));
    }
    let proxy = req.connect().context("error connecting to Echo service")?;
    let res = proxy.echo_string(Some(TEST_STRING)).await?;
    assert_eq!(res.as_deref(), Some(TEST_STRING));
    Ok(())
}
