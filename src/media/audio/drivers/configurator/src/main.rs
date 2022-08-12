// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    fuchsia_fs::OpenFlags,
    fuchsia_inspect::{component, health::Reporter},
    futures::lock::Mutex,
    std::sync::Arc,
    tracing,
};

mod codec;
mod config;
mod configurator;
mod dai;
mod default;
mod discover;
mod indexes;
mod testing;

use crate::{config::Config, configurator::Configurator, default::DefaultConfigurator};

#[fuchsia::main(logging = true)]
async fn main() -> Result<(), anyhow::Error> {
    component::health().set_ok();
    tracing::trace!("Initialized.");
    let codec_proxy =
        fuchsia_fs::directory::open_in_namespace("/dev/class/codec", OpenFlags::RIGHT_READABLE)?;
    let dai_proxy =
        fuchsia_fs::directory::open_in_namespace("/dev/class/dai", OpenFlags::RIGHT_READABLE)?;
    let mut config = Config::new()?;
    config.load()?;
    let configurator = Arc::new(Mutex::new(DefaultConfigurator::new(config)?));
    let codec_future = discover::find_codecs(codec_proxy, 0, configurator.clone());
    let dai_future = discover::find_dais(dai_proxy, 0, configurator);
    match futures::try_join!(codec_future, dai_future) {
        Ok(value) => {
            tracing::error!("Find devices returned: {:?}", value);
            return Err(anyhow!("Find devices returned: {:?}", value));
        }
        Err(e) => {
            tracing::error!("Find devices error: {:?}", e);
            return Err(e);
        }
    };
}
