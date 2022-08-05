// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod launcher;

use {
    crate::launcher::FuzzComponentLauncher,
    test_runners_elf_lib::{runner, test_server},
};

type FuzzTestServer = test_server::TestServer<FuzzComponentLauncher>;

fn get_test_server() -> FuzzTestServer {
    FuzzTestServer { launcher: FuzzComponentLauncher::new() }
}

#[fuchsia::main(logging_tags=["fuzz_test_runner"])]
fn main() -> Result<(), anyhow::Error> {
    runner::add_runner_service(get_test_server, FuzzTestServer::validate_args)
}
