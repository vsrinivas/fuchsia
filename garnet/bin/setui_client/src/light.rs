// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl_fuchsia_settings::{LightProxy, LightState};

use crate::LightGroup;

pub async fn command(proxy: LightProxy, light_group: LightGroup) -> Result<String, Error> {
    let has_name = light_group.name.is_some();
    let has_simple_value = light_group.simple.len() > 0;
    let has_brightness = light_group.brightness.len() > 0;
    let has_rgb = light_group.rgb.len() > 0;

    if !has_name && !has_simple_value && !has_brightness && !has_rgb {
        // No values set, perform a watch instead.
        let setting_value = proxy.watch_light_groups().await?;
        return Ok(format!("{:#?}", setting_value));
    }

    if !has_name {
        return Err(format_err!("light group name required"));
    }

    if !has_simple_value && !has_brightness && !has_rgb {
        return Err(format_err!("light value required"));
    }

    let light_states: Vec<LightState> = light_group.clone().into();

    let light_state_str = format!("{:?}", light_states);
    let result = proxy
        .set_light_group_values(
            light_group.name.clone().unwrap().as_str(),
            &mut light_states.into_iter(),
        )
        .await?;
    return match result {
        Ok(_) => Ok(format!(
            "Successfully set light group {} with values {:?}",
            light_group.name.unwrap(),
            light_state_str
        )),
        Err(err) => Ok(format!("{:#?}", err)),
    };
}
