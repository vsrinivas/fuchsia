// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;

mod app;
mod graphics_util;
mod view;

// TODO(38577): Write example tests for the graphical session.

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut app = app::App::new().await?;
    app.run().await
}
