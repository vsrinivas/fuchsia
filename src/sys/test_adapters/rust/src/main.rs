// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod rust_test_adapter;

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_test as ftest,
    fsyslog::fx_log_info,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog as fsyslog,
    futures::prelude::*,
    rust_test_adapter::{RustTestAdapter, TestInfo},
    std::env,
};

/// Parses the incoming arguments into a test to run and any arguments to that test
fn consume_args(args: Vec<String>) -> Result<TestInfo, Error> {
    if args.len() < 2 {
        return Err(format_err!("Usage: rust_test_adapter <test path in pkg>"));
    }

    let test_path = String::from(&args[1]);
    let test_args = args[2..].to_vec();

    Ok(TestInfo { test_path, test_args })
}

fn main() -> Result<(), Error> {
    fsyslog::init_with_tags(&["rust_test_adapter"])?;
    fx_log_info!("adapter started");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let mut fs = ServiceFs::new_local();

    fs.dir("svc").add_fidl_service(move |stream: ftest::SuiteRequestStream| {
        fasync::spawn_local(async move {
            let test_info = consume_args(env::args().collect()).unwrap();
            let adapter = RustTestAdapter::new(test_info).expect("Failed to create adapter");
            adapter.run_test_suite(stream).await.expect("Failed to run test suite");
        });
    });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_no_args() {
        let args = vec![String::from("my_package"), String::from("my_test")];
        let expected_info = TestInfo { test_path: String::from("my_test"), test_args: vec![] };
        let actual_info = consume_args(args).unwrap();

        assert_eq!(expected_info, actual_info);
    }

    #[test]
    fn parse_one_arg() {
        let args = vec![String::from("my_package"), String::from("my_test"), String::from("one")];
        let expected_info =
            TestInfo { test_path: String::from("my_test"), test_args: vec![String::from("one")] };
        let actual_info = consume_args(args).unwrap();

        assert_eq!(expected_info, actual_info);
    }

    #[test]
    fn parse_multiple_args() {
        let args = vec![
            String::from("my_package"),
            String::from("my_test"),
            String::from("one"),
            String::from("two"),
        ];
        let expected_info = TestInfo {
            test_path: String::from("my_test"),
            test_args: vec![String::from("one"), String::from("two")],
        };
        let actual_info = consume_args(args).unwrap();

        assert_eq!(expected_info, actual_info);
    }
}
