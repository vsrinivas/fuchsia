// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service, std::fs::read_to_string,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let pseudo_dir_data = read_to_string("/foo/bar/baz")?;
    let test_ns_data = read_to_string("/test_pkg/meta/package").unwrap();
    let echo = connect_to_service::<fecho::EchoMarker>()?;
    echo.echo_string(Some(&pseudo_dir_data)).await?;
    echo.echo_string(Some(&test_ns_data)).await?;
    Ok(())
}
