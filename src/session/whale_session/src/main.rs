// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;

mod app;
mod graphics_util;
mod view;

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut app = app::App::new().await?;
    app.run().await
}
