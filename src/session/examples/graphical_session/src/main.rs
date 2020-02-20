// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;

mod app;
mod view;

/// Entry point for the `graphical_session`. It creates an instance of the `App`, runs it, and
/// waits for it to finish.
#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut app = app::App::new().await?;
    app.run().await
}
