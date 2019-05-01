// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    failure::Error,
    fidl_fuchsia_test_echos::{
        EchoExposedByParentMarker,
        EchoHiddenByParentMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon as zx,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Check that only the service provided by the inner component is available,
    // and that we're connected to its implementation of the service which always
    // returns `42` rather than echoing.
    let echo = connect_to_service::<EchoExposedByParentMarker>()?;
    assert_eq!(42, await!(echo.echo(1))?);
    let echo = connect_to_service::<EchoHiddenByParentMarker>()?;
    match await!(echo.echo(2)) {
        Err(fidl::Error::ClientRead(zx::Status::PEER_CLOSED)) |
        Err(fidl::Error::ClientWrite(zx::Status::PEER_CLOSED)) => {}
        Ok(_) => panic!("inner nested component succesfully echoed through parent"),
        Err(e) => panic!("Unexpected error connecting to hidden service: {:?}", e),
    }
    Ok(())
}
