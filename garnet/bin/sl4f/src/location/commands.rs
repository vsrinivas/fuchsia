// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{location::regulatory_region_facade::RegulatoryRegionFacade, server::Facade};
use anyhow::{format_err, Error};
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{to_value, Value};

impl Facade for RegulatoryRegionFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        regulatory_region_method_to_fidl(method, args, self).boxed_local()
    }
}

async fn regulatory_region_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &RegulatoryRegionFacade,
) -> Result<Value, Error> {
    match method_name.as_ref() {
        "set_region" => {
            let region =
                args.get("region").ok_or_else(|| format_err!("Must provide a `region`"))?;
            let region = region.as_str().ok_or_else(|| format_err!("`region` must be a string"))?;
            Ok(to_value(facade.set_region(region)?)?)
        }
        _ => {
            return Err(format_err!(
                "unsupported command {} for regulatory-region-facade!",
                method_name
            ))
        }
    }
}
