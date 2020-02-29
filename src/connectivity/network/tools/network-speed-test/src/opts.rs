// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use structopt::StructOpt;

#[derive(StructOpt, Clone, Debug)]
/// Simple integration test estimating observed bandwidth measured in
/// Megabits per second on file download:
/// currently a single http get over the default route/interface.
pub struct Opt {
    /// url to retrieve for the test
    #[structopt(
        short = "u",
        long = "target_url",
        default_value = "https://dl.google.com/dl/android/aosp/taimen-opd1.170816.010-factory-c796ddb4.zip"
    )]
    pub target_url: String,
}
