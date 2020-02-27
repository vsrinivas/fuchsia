// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_location_namedplace::{
    RegulatoryRegionConfiguratorMarker, RegulatoryRegionConfiguratorProxy,
};
use fuchsia_component::client::connect_to_service;

#[derive(Debug)]
pub struct RegulatoryRegionFacade {
    configurator: RegulatoryRegionConfiguratorProxy,
}

impl RegulatoryRegionFacade {
    pub fn new() -> Result<RegulatoryRegionFacade, Error> {
        Ok(RegulatoryRegionFacade {
            configurator: connect_to_service::<RegulatoryRegionConfiguratorMarker>()?,
        })
    }

    /// Informs the RegulatoryRegionService of the new `region_code`.
    pub fn set_region(&self, region_code: &str) -> Result<(), Error> {
        self.configurator.set_region(region_code)?;
        Ok(())
    }
}
