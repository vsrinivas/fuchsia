// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::scrutiny::Scrutiny,
    anyhow::Result,
    log::info,
    scrutiny_plugins::{
        core::CorePlugin, engine::EnginePlugin, search::SearchPlugin, toolkit::ToolkitPlugin,
    },
};

/// Provides a default launcher for the Scrutiny frontend. This is intended to
/// be used by binaries that wish to launch a full copy of the Scrutiny
/// framework with default settings.
pub fn launch() -> Result<()> {
    info!("Scrutiny: Launching with default launcher");
    let mut app = Scrutiny::new(Scrutiny::args_from_env()?)?;
    info!("Scrutiny: Registering & Loading Core Plugin");
    app.plugin(CorePlugin::new())?;
    info!("Scrutiny: Registering & Loading Search Plugin");
    app.plugin(SearchPlugin::new())?;
    info!("Scrutiny: Registering & Loading Engine Plugin");
    app.plugin(EnginePlugin::new(app.scheduler(), app.dispatcher(), app.plugin_manager()))?;
    info!("Scrutiny: Registering & Loading Toolkit Plugin");
    app.plugin(ToolkitPlugin::new())?;
    info!("Scrutiny: Starting Framework Runtime");
    app.run()
}
