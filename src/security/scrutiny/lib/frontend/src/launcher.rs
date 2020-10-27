// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::scrutiny::Scrutiny,
    anyhow::Result,
    scrutiny_plugins::{
        core::core::CorePlugin, engine::EnginePlugin, search::SearchPlugin, toolkit::ToolkitPlugin,
    },
};

/// Provides a default launcher for the Scrutiny frontend. This is intended to
/// be used by binaries that wish to launch a full copy of the Scrutiny
/// framework with default settings.
pub fn launch() -> Result<()> {
    let mut app = Scrutiny::new(Scrutiny::args_from_env()?)?;
    app.plugin(CorePlugin::new())?;
    app.plugin(SearchPlugin::new())?;
    app.plugin(EnginePlugin::new(app.scheduler(), app.dispatcher(), app.plugin_manager()))?;
    app.plugin(ToolkitPlugin::new())?;
    app.run()
}
