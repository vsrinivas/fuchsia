// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, ascendd_lib::run_ascendd};

fn main() -> Result<(), Error> {
    hoist::logger::init()?;
    hoist::run(run_ascendd(argh::from_env()))
}
