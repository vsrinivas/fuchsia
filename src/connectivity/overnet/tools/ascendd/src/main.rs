// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, ascendd_lib::run_ascendd};

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    run_ascendd(argh::from_env(), Box::new(async_std::io::stderr())).await
}
