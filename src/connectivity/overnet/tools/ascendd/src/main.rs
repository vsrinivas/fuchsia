// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, ascendd_lib::run_ascendd, futures::prelude::*, tokio::runtime::current_thread,
};

async fn async_main() -> Result<(), Error> {
    hoist::logger::init()?;

    run_ascendd(argh::from_env()).await
}

fn main() {
    current_thread::run(
        (async move {
            if let Err(e) = async_main().await {
                log::warn!("Error: {}", e);
            }
        })
        .unit_error()
        .boxed_local()
        .compat(),
    );
}
