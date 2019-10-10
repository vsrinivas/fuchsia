// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fidl_fuchsia_location_namedplace::RegulatoryRegionConfiguratorMarker;
use fuchsia_async;
use fuchsia_component::client::connect_to_service;

const REGION: &'static str = "WW";

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let region_configurator = connect_to_service::<RegulatoryRegionConfiguratorMarker>()
        .context("Failed to connect to Configurator protocol")?;
    region_configurator.set_region(REGION)?;
    Ok(())
}
