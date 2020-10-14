// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod logo;
mod rest;
mod scrutiny;
mod shell;

use {
    crate::scrutiny::ScrutinyApp,
    anyhow::Result,
    scrutiny_plugins::{
        core::core::CorePlugin, engine::EnginePlugin, search::SearchPlugin, toolkit::ToolkitPlugin,
    },
};

fn main() -> Result<()> {
    let mut app = ScrutinyApp::new(ScrutinyApp::args())?;
    app.plugin(CorePlugin::new())?;
    app.plugin(SearchPlugin::new())?;
    app.plugin(EnginePlugin::new(app.scheduler(), app.dispatcher(), app.plugin_manager()))?;
    app.plugin(ToolkitPlugin::new())?;
    app.run()
}
