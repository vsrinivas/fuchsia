// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod app;

use {
    anyhow::Result,
    app::app::ScrutinyApp,
    scrutiny::plugins::{
        components::graph::ComponentGraphPlugin, health::HealthPlugin,
        management::ManagementPlugin, search::ModelSearchPlugin,
    },
};

fn main() -> Result<()> {
    let mut app = ScrutinyApp::new(ScrutinyApp::args())?;
    app.plugin(HealthPlugin::new())?;
    app.plugin(ComponentGraphPlugin::new())?;
    app.plugin(ModelSearchPlugin::new())?;
    app.plugin(ManagementPlugin::new(app.scheduler(), app.dispatcher(), app.plugin_manager()))?;
    app.run()
}
