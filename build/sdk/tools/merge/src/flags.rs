// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use structopt::StructOpt;

#[derive(Debug, StructOpt)]
pub struct Flags {
    #[structopt(short = "b", long = "base-archive")]
    pub base: String,

    #[structopt(short = "c", long = "complement-archive")]
    pub complement: String,

    #[structopt(short = "o", long = "output-archive")]
    pub output: String,
}
