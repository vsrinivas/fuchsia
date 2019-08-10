// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    fdio,
    fidl_fuchsia_sys::FileDescriptor,
    fuchsia_async as fasync,
    fuchsia_component::client::{self, LaunchOptions},
    fuchsia_runtime::HandleType,
    std::io::{BufRead, BufReader},
};

macro_rules! test_elf_runner {
    (
        $(
            $test_name:ident => {
                url = $url:expr,
                expected_output = $expected_output:expr,
            },
        )+
    ) => {
        $(
            #[fasync::run_singlethreaded(test)]
            async fn $test_name() {
                run_single_test($url, $expected_output);
            }
        )+
    }

}

test_elf_runner! {
    echo_with_args => {
        url = "fuchsia-pkg://fuchsia.com/elf_runner_test#meta/echo_args.cm",
        expected_output = "/pkg/bin/echo_args Hippos rule!\n",
    },
    echo_without_args => {
        url = "fuchsia-pkg://fuchsia.com/elf_runner_test#meta/echo_no_args.cm",
        expected_output = "/pkg/bin/echo_args\n",
    },
}

fn run_single_test(url: &str, expected_output: &str) {
    let launcher = client::launcher().expect("failed to connect to launcher");
    let (pipe_handle, socket_handle) = fdio::pipe_half().expect("failed to create pipe");
    let mut options = LaunchOptions::new();
    options.set_out(FileDescriptor {
        type0: HandleType::FileDescriptor as i32,
        type1: 0,
        type2: 0,
        handle0: Some(socket_handle.into()),
        handle1: None,
        handle2: None,
    });

    let _app = client::launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/elf_runner_test#meta/component_manager.cmx".to_string(),
        Some(vec![url.to_string()]),
        options,
    )
    .expect("component manager failed to launch");

    let mut reader = BufReader::new(pipe_handle);
    let mut line = String::new();
    reader.read_line(&mut line).expect("failed to read echo_args output");
    assert_eq!(&line, expected_output);
}
