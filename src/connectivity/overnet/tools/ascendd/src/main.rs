// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, argh::FromArgs, ascendd_lib::run_ascendd, futures::prelude::*,
    tokio::runtime::current_thread,
};

#[derive(FromArgs)]
/// daemon to lift a non-Fuchsia device into Overnet.
struct Opt {
    #[argh(option, long = "sockpath")]
    /// path to the ascendd socket.
    /// If not provided, this will default to a new socket-file in /tmp.
    sockpath: Option<String>,
}

async fn async_main() -> Result<(), Error> {
    hoist::logger::init()?;
    let Opt { sockpath } = argh::from_env();

    let sockpath = sockpath.unwrap_or(hoist::DEFAULT_ASCENDD_PATH.to_string());

    run_ascendd(sockpath).await
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
