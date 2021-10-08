// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use test_runners_elf_lib::{
    launcher::ElfComponentLauncher, runner::add_runner_service, test_server::TestServer,
};

type ElfTestServer = TestServer<ElfComponentLauncher>;

fn get_test_server() -> ElfTestServer {
    TestServer { launcher: ElfComponentLauncher::new() }
}

fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init_with_tags(&["elf_test_runner"])?;
    add_runner_service(get_test_server, ElfTestServer::validate_args)
}
