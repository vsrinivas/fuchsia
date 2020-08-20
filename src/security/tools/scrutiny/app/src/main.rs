// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod args;
mod builtin;
mod error;
mod logo;
mod scrutiny;
mod shell;

use {
    crate::scrutiny::ScrutinyApp,
    anyhow::Result,
    scrutiny_plugins::{core::core::CorePlugin, engine::EnginePlugin, model::ModelPlugin},
};

fn main() -> Result<()> {
    let mut app = ScrutinyApp::new(ScrutinyApp::args())?;
    app.plugin(CorePlugin::new())?;
    app.plugin(ModelPlugin::new())?;
    app.plugin(EnginePlugin::new(app.scheduler(), app.dispatcher(), app.plugin_manager()))?;
    app.run()
}
