// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect::{component, health::Reporter};
use io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE};
use tracing;

mod codec;
mod configurator;
mod default;
mod discover;
mod testing;

use crate::configurator::Configurator;
use crate::default::DefaultConfigurator;

#[fuchsia::component(logging = true)]
async fn main() -> Result<(), anyhow::Error> {
    component::health().set_ok();
    tracing::trace!("Initialized.");
    let dev_proxy = open_directory_in_namespace("/dev/class/codec", OPEN_RIGHT_READABLE)?;
    let configurator = DefaultConfigurator::new();
    discover::find_codecs(dev_proxy, false, configurator).await?;
    unreachable!();
}
