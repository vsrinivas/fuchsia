// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    failure::{format_err, Error, ResultExt},
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let echo = connect_to_service::<fecho::EchoMarker>().context("error connecting to echo")?;
    let out = await!(echo.echo_string(Some("Hippos rule!"))).context("echo_string failed")?;
    println!("{}", out.ok_or(format_err!("empty result"))?);
    Ok(())
}
