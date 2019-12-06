// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod gtest_adapter;

use {
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_test as ftest, fuchsia_async as fasync,
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
            run_test_suite(adapter, stream).await.expect("failed to run test suite service")
        });
    });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}

async fn run_test_suite(
    adapter: GTestAdapter,
    mut stream: ftest::SuiteRequestStream,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            ftest::SuiteRequest::GetTests { responder } => {
                let tests = adapter.enumerate_tests().await?;
                responder
                    .send(&mut tests.into_iter().map(|name| ftest::Case { name: Some(name) }))
                    .context("Cannot send fidl response")?;
            }
            ftest::SuiteRequest::Run { tests: _, options: _, run_listener: _, .. } => {
                return Err(format_err!("We don't support running tests yet!!!."));
            }
        }
    }
    Ok(())
}
