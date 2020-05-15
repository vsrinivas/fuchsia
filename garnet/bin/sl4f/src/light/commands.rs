// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::light::facade::LightFacade;
use crate::light::types::{LightMethod, SerializableRgb};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

fn get_index(args: Value) -> Result<u32, Error> {
    // Serde json does not support as_u32 so you need to take care of the cast.
    let index = match args.get("index") {
        Some(value) => match value.as_u64() {
            Some(v) => v as u32,
            None => bail!("Expected u64 type for index."),
        },
        None => bail!("Expected a serde_json Value index."),
    };
    Ok(index)
}

#[async_trait(?Send)]
impl Facade for LightFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match LightMethod::from_str(&method) {
            LightMethod::GetNumLights => {
                let result = self.get_num_lights().await?;
                Ok(to_value(result)?)
            }

            LightMethod::GetNumLightGroups => {
                let result = self.get_num_light_groups().await?;
                Ok(to_value(result)?)
            }

            LightMethod::GetInfo => {
                let index = get_index(args)?;
                let result = self.get_info(index).await?;
                Ok(to_value(result)?)
            }

            LightMethod::GetCurrentSimpleValue => {
                let index = get_index(args)?;
                let result = self.get_current_simple_value(index).await?;
                Ok(to_value(result)?)
            }

            LightMethod::SetSimpleValue => {
                let index = get_index(args.clone())?;
                let val = match args.get("value") {
                    Some(x) => match x.clone().as_bool() {
                        Some(v) => v,
                        None => bail!("Expected a boolean value."),
                    },
                    None => bail!("Expected a serde_json Value value."),
                };
                let result = self.set_simple_value(index, val).await?;
                Ok(to_value(result)?)
            }

            LightMethod::GetCurrentBrightnessValue => {
                let index = get_index(args)?;
                let result = self.get_current_brightness_value(index).await?;
                Ok(to_value(result)?)
            }

            LightMethod::SetBrightnessValue => {
                let index = get_index(args.clone())?;
                let val = match args.get("value") {
                    Some(x) => match x.clone().as_u64() {
                        Some(v) => v as u8,
                        None => bail!("Expected a uint8 value."),
                    },
                    None => bail!("Expected a serde_json Value value."),
                };
                let result = self.set_brightness_value(index, val).await?;
                Ok(to_value(result)?)
            }

            LightMethod::GetCurrentRgbValue => {
                let index = get_index(args)?;
                let result = self.get_current_rgb_value(index).await?;
                Ok(to_value(result)?)
            }

            LightMethod::SetRgbValue => {
                let index = get_index(args.clone())?;
                let rgb = match args.get("value") {
                    Some(v) => v,
                    None => bail!("Expected a serde_json Value value"),
                };
                let val = SerializableRgb {
                    red: rgb["red"].as_u64().unwrap() as u8,
                    green: rgb["green"].as_u64().unwrap() as u8,
                    blue: rgb["blue"].as_u64().unwrap() as u8,
                };
                let result = self.set_rgb_value(index, val).await?;
                Ok(to_value(result)?)
            }

            LightMethod::GetGroupInfo => {
                let index = get_index(args)?;
                let result = self.get_group_info(index).await?;
                Ok(to_value(result)?)
            }

            LightMethod::GetGroupCurrentSimpleValue => {
                let index = get_index(args)?;
                let result = self.get_group_current_simple_value(index).await?;
                Ok(to_value(result)?)
            }

            LightMethod::SetGroupSimpleValue => {
                let index = get_index(args.clone())?;
                let val = match serde_json::from_value(args.get("values").unwrap().clone()) {
                    Ok(x) => x,
                    Err(e) => bail!("Expected a serde_json Value values {:?}", e),
                };
                let result = self.set_group_simple_value(index, val).await?;
                Ok(to_value(result)?)
            }

            LightMethod::GetGroupCurrentBrightnessValue => {
                let index = get_index(args)?;
                let result = self.get_group_current_brightness_value(index).await?;
                Ok(to_value(result)?)
            }

            LightMethod::SetGroupBrightnessValue => {
                let index = get_index(args.clone())?;
                let val = match serde_json::from_value(args.get("values").unwrap().clone()) {
                    Ok(x) => x,
                    Err(e) => bail!("Expected a serde_json Value values {:?}", e),
                };
                let result = self.set_group_brightness_value(index, val).await?;
                Ok(to_value(result)?)
            }

            LightMethod::GetGroupCurrentRgbValue => {
                let index = get_index(args)?;
                let result = self.get_group_current_rgb_value(index).await?;
                Ok(to_value(result)?)
            }

            LightMethod::SetGroupRgbValue => {
                let index = get_index(args.clone())?;
                let val = match serde_json::from_value(args.get("values").unwrap().clone()) {
                    Ok(x) => x,
                    Err(e) => bail!("Expected a serde_json Value values {:?}", e),
                };
                let result = self.set_group_rgb_value(index, val).await?;
                Ok(to_value(result)?)
            }

            _ => bail!("Invalid Light Facade FIDL method: {:?}", method),
        }
    }
}
