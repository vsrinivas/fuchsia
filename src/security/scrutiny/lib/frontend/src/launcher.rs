// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{config::Config, scrutiny::Scrutiny},
    anyhow::Result,
    scrutiny_plugins::{
        core::CorePlugin, engine::EnginePlugin, search::SearchPlugin, toolkit::ToolkitPlugin,
        verify::VerifyPlugin,
    },
};

/// Launches scrutiny from a configuration file. This is intended to be used by binaries that
/// want to launch custom configurations of the Scrutiny framework with select features enabled.
pub fn launch_from_config(config: Config) -> Result<()> {
    let mut scrutiny = Scrutiny::new(config)?;
    scrutiny.plugin(CorePlugin::new())?;
    scrutiny.plugin(SearchPlugin::new())?;
    scrutiny.plugin(EnginePlugin::new(
        scrutiny.scheduler(),
        scrutiny.dispatcher(),
        scrutiny.plugin_manager(),
    ))?;
    scrutiny.plugin(ToolkitPlugin::new())?;
    scrutiny.plugin(VerifyPlugin::new())?;
    scrutiny.run()
}

/// Provides a default launcher for the Scrutiny frontend. This is intended to
/// be used by binaries that wish to launch a full copy of the Scrutiny
/// framework with default settings.
pub fn launch() -> Result<()> {
    launch_from_config(Scrutiny::args_from_env()?)
}
