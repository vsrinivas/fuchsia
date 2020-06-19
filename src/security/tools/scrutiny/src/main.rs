// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod app;

use {
    anyhow::Result,
    app::app::ScrutinyApp,
    scrutiny::{plugins::components::graph::ComponentGraphPlugin, plugins::health::HealthPlugin},
};

fn main() -> Result<()> {
    let mut app = ScrutinyApp::new(ScrutinyApp::args())?;
    app.plugin(HealthPlugin::new())?;
    app.plugin(ComponentGraphPlugin::new())?;
    app.run()
}
