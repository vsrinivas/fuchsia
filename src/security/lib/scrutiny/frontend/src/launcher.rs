// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::scrutiny::Scrutiny,
    anyhow::Result,
    scrutiny_config::{Config, ConfigBuilder, ModelConfig},
    scrutiny_plugins::{
        core::CorePlugin, devmgr_config::DevmgrConfigPlugin, engine::EnginePlugin,
        search::SearchPlugin, static_pkgs::StaticPkgsPlugin, sys::SysRealmPlugin,
        toolkit::ToolkitPlugin, verify::VerifyPlugin,
    },
    std::sync::Arc,
};

/// Launches scrutiny from a configuration file. This is intended to be used by binaries that
/// want to launch custom configurations of the Scrutiny framework with select features enabled.
pub fn launch_from_config(config: Config) -> Result<String> {
    let model_is_empty = config.runtime.model.is_empty();

    let mut scrutiny = Scrutiny::new(config)?;
    scrutiny.plugin(EnginePlugin::new(
        scrutiny.scheduler(),
        scrutiny.dispatcher(),
        Arc::downgrade(&scrutiny.plugin_manager()),
    ))?;
    scrutiny.plugin(ToolkitPlugin::new())?;

    // These plugins only apply when the model contains valid paths, because the blobs and update
    // package must be present.
    if !model_is_empty {
        scrutiny.plugin(DevmgrConfigPlugin::new())?;
        scrutiny.plugin(StaticPkgsPlugin::new())?;
        scrutiny.plugin(CorePlugin::new())?;
        scrutiny.plugin(VerifyPlugin::new())?;
        scrutiny.plugin(SysRealmPlugin::new())?;
        scrutiny.plugin(SearchPlugin::new())?;
    }
    scrutiny.run()
}

/// Provides a utility launcher for the Scruity frontend. This is intended to
/// be used by consumer libraries that simply want to launch the framework to
/// run a single command.
pub fn run_command(command: String) -> Result<String> {
    let model = ModelConfig::empty();
    let config = ConfigBuilder::with_model(model).command(command).build();
    launch_from_config(config)
}

/// Provides a utility launcher for the Scrutiny frontend. This is inteded to
/// be used by consumer libraries that simply want to launch the framework to
/// run a Scrutiny script.
pub fn run_script(script_path: String) -> Result<String> {
    let model = ModelConfig::empty();
    let config = ConfigBuilder::with_model(model).script(script_path).build();
    launch_from_config(config)
}
