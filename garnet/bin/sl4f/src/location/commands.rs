// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    location::{
        emergency_provider_facade::EmergencyProviderFacade,
        regulatory_region_facade::RegulatoryRegionFacade, types::PositionSerializer,
    },
    server::Facade,
};
use anyhow::{format_err, Error};
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for RegulatoryRegionFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "set_region" => {
                let region =
                    args.get("region").ok_or_else(|| format_err!("Must provide a `region`"))?;
                let region =
                    region.as_str().ok_or_else(|| format_err!("`region` must be a string"))?;
                Ok(to_value(self.set_region(region)?)?)
            }
            _ => {
                return Err(format_err!(
                    "unsupported command {} for regulatory-region-facade!",
                    method
                ))
            }
        }
    }
}

#[async_trait(?Send)]
impl Facade for EmergencyProviderFacade {
    async fn handle_request(&self, method: String, _args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "get_current" => Ok(to_value(PositionSerializer(self.get_current().await?))?),
            _ => {
                return Err(format_err!(
                    "unsupported command {} for emergency-provider-facade!",
                    method
                ))
            }
        }
    }
}
