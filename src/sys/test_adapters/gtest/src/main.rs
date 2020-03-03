// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod gtest_adapter;

use {
    anyhow::{format_err, Context as _, Error},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::fx_log_info,
    futures::prelude::*,
    gtest_adapter::GTestAdapter,
    std::env,
};

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["gtest_adapter"])?;
    fx_log_info!("adapter started");
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let args: Vec<String> = env::args().collect();
    if args.len() != 2 {
        return Err(format_err!("Usage: gtest_adapter <test path in pkg>"));
    }
    let test = &args[1];
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        let adapter = GTestAdapter::new(test.to_string()).expect("Cannot create adapter");
        fasync::spawn_local(async move {
            adapter.run_test_suite(stream).await.expect("failed to run test suite service")
        });
    });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}
