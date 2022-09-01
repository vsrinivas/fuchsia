// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    anyhow::anyhow, anyhow::Result, args::AddTestNodeCommand,
    fidl_fuchsia_driver_development as fdd, fidl_fuchsia_driver_framework as fdf,
};

fn string_to_property(prop: &str) -> Result<fdf::NodeProperty> {
    let split: Vec<&str> = prop.split("=").collect();
    if split.len() != 2 {
        return Err(anyhow!("Bad Property '{}', properties need one '=' character", prop));
    }

    Ok(fdf::NodeProperty {
        key: Some(fdf::NodePropertyKey::StringValue(split[0].to_string())),
        value: Some(fdf::NodePropertyValue::StringValue(split[1].to_string())),
        ..fdf::NodeProperty::EMPTY
    })
}

pub async fn add_test_node(
    cmd: &AddTestNodeCommand,
    driver_development_proxy: fdd::DriverDevelopmentProxy,
) -> Result<()> {
    driver_development_proxy
        .add_test_node(fdd::TestNodeAddArgs {
            name: Some(cmd.name.clone()),
            properties: Some(vec![string_to_property(&cmd.property)?]),
            ..fdd::TestNodeAddArgs::EMPTY
        })
        .await?
        .map_err(|e| anyhow!("Calling AddTestNode failed with {:#?}", e))?;
    Ok(())
}
