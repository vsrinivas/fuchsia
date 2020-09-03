// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "test", description = "run tests")]
pub struct TestCommand {
    /// test timeout.
    #[argh(option, short = 't')]
    pub timeout: Option<u32>,

    /// test url. Test should implement `fuchsia.test.Suite` protocol.
    #[argh(positional)]
    pub test_url: String,

    /// test filter. A glob pattern for matching tests.
    #[argh(option)]
    pub test_filter: Option<String>,

    #[argh(switch)]
    /// list tests in the Test Suite
    pub list: bool,

    /// whether to also run tests that have been marked disabled/ignored by the test author.
    #[argh(switch)]
    pub also_run_disabled_tests: bool,

    /// run test cases in parallel.
    #[argh(option)]
    pub parallel: Option<u16>,
}
