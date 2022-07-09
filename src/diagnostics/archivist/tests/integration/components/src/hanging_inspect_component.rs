// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use anyhow::Error;
use fuchsia_async::Timer;
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect::{component, Inspector};
use fuchsia_zircon::Duration;
use futures::{FutureExt, StreamExt};

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    let insp = component::inspector();
    insp.root().record_string("child", "value");

    insp.root().record_lazy_values("lazy-node-always-hangs", || {
        async move {
            Timer::new(Duration::from_minutes(60)).await;
            Ok(Inspector::new())
        }
        .boxed()
    });

    insp.root().record_int("int", 3);

    let mut fs = ServiceFs::new();
    inspect_runtime::serve(&insp, &mut fs)?;
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}
