// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common_utils::common::macros::{fx_err_and_bail, with_line},
    light::types::{SerializableGroupInfo, SerializableInfo, SerializableRgb},
};
use anyhow::Error;
use fidl_fuchsia_hardware_light::{LightMarker, LightProxy, Rgb};
use fuchsia_syslog::macros::*;
use glob::glob;

/// Perform Light operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct LightFacade {}

impl LightFacade {
    pub fn new() -> LightFacade {
        LightFacade {}
    }

    fn get_proxy(&self) -> Result<LightProxy, Error> {
        let tag = "LightFacade::get_proxy";
        let (proxy, server) = match fidl::endpoints::create_proxy::<LightMarker>() {
            Ok(r) => r,
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("Failed to get light proxy {:?}", e))
            }
        };

        let found_path = glob("/dev/class/light/*")?.filter_map(|entry| entry.ok()).next();
        match found_path {
            Some(path) => {
                fdio::service_connect(path.to_string_lossy().as_ref(), server.into_channel())?;
                Ok(proxy)
            }
            None => fx_err_and_bail!(&with_line!(tag), format_err!("Failed to find device")),
        }
    }

    pub async fn get_num_lights(&self) -> Result<u32, Error> {
        Ok(self.get_proxy()?.get_num_lights().await?)
    }

    pub async fn get_num_light_groups(&self) -> Result<u32, Error> {
        Ok(self.get_proxy()?.get_num_light_groups().await?)
    }

    pub async fn get_info(&self, index: u32) -> Result<SerializableInfo, Error> {
        let tag = "LightFacade::get_info";
        match self.get_proxy()?.get_info(index).await? {
            Ok(r) => Ok(SerializableInfo::new(&r)),
            Err(e) => fx_err_and_bail!(&with_line!(tag), format_err!("GetInfo failed {:?}", e)),
        }
    }

    pub async fn get_current_simple_value(&self, index: u32) -> Result<bool, Error> {
        let tag = "LightFacade::get_current_simple_value";
        match self.get_proxy()?.get_current_simple_value(index).await? {
            Ok(r) => Ok(r),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("GetCurrentSimpleValue failed {:?}", e)
            ),
        }
    }

    pub async fn set_simple_value(&self, index: u32, value: bool) -> Result<(), Error> {
        let tag = "LightFacade::set_simple_value";
        match self.get_proxy()?.set_simple_value(index, value).await? {
            Ok(r) => Ok(r),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("SetSimpleValue failed {:?}", e))
            }
        }
    }

    pub async fn get_current_brightness_value(&self, index: u32) -> Result<u8, Error> {
        let tag = "LightFacade::get_current_brightness_value";
        match self.get_proxy()?.get_current_brightness_value(index).await? {
            Ok(r) => Ok(r),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("GetCurrentBrightnessValue failed {:?}", e)
            ),
        }
    }

    pub async fn set_brightness_value(&self, index: u32, value: u8) -> Result<(), Error> {
        let tag = "LightFacade::set_brightness_value";
        match self.get_proxy()?.set_brightness_value(index, value).await? {
            Ok(r) => Ok(r),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("SetBrightnessValue failed {:?}", e))
            }
        }
    }

    pub async fn get_current_rgb_value(&self, index: u32) -> Result<SerializableRgb, Error> {
        let tag = "LightFacade::get_current_rgb_value";
        match self.get_proxy()?.get_current_rgb_value(index).await? {
            Ok(r) => Ok(SerializableRgb::new(&r)),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("GetCurrentRgbValue failed {:?}", e))
            }
        }
    }

    pub async fn set_rgb_value(&self, index: u32, value: SerializableRgb) -> Result<(), Error> {
        let tag = "LightFacade::set_rgb_value";
        let mut rgb = Rgb { red: value.red, green: value.green, blue: value.blue };
        match self.get_proxy()?.set_rgb_value(index, &mut rgb).await? {
            Ok(r) => Ok(r),
            Err(e) => fx_err_and_bail!(&with_line!(tag), format_err!("SetRgbValue failed {:?}", e)),
        }
    }

    pub async fn get_group_info(&self, index: u32) -> Result<SerializableGroupInfo, Error> {
        let tag = "LightFacade::get_group_info";
        match self.get_proxy()?.get_group_info(index).await? {
            Ok(r) => Ok(SerializableGroupInfo::new(&r)),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("GetGroupInfo failed {:?}", e))
            }
        }
    }

    pub async fn get_group_current_simple_value(&self, index: u32) -> Result<Vec<bool>, Error> {
        let tag = "LightFacade::get_group_current_simple_value";
        match self.get_proxy()?.get_group_current_simple_value(index).await? {
            Ok(r) => match r {
                Some(v) => Ok(v),
                None => fx_err_and_bail!(&with_line!(tag), format_err!("Expected a vector")),
            },
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("GetGroupCurrentSimpleValue failed {:?}", e)
            ),
        }
    }

    pub async fn set_group_simple_value(&self, index: u32, value: Vec<bool>) -> Result<(), Error> {
        let tag = "LightFacade::set_group_simple_value";
        match self.get_proxy()?.set_group_simple_value(index, &mut value.into_iter()).await? {
            Ok(_) => Ok(()),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("SetGroupSimpleValue failed {:?}", e)
            ),
        }
    }

    pub async fn get_group_current_brightness_value(&self, index: u32) -> Result<Vec<u8>, Error> {
        let tag = "LightFacade::get_group_current_brightness_value";
        match self.get_proxy()?.get_group_current_brightness_value(index).await? {
            Ok(r) => match r {
                Some(v) => Ok(v),
                None => fx_err_and_bail!(&with_line!(tag), format_err!("Expected a vector")),
            },
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("GetGroupCurrentBrightnessValue failed {:?}", e)
            ),
        }
    }

    pub async fn set_group_brightness_value(
        &self,
        index: u32,
        value: Vec<u8>,
    ) -> Result<(), Error> {
        let tag = "LightFacade::set_group_brightness_value";
        match self.get_proxy()?.set_group_brightness_value(index, &value).await? {
            Ok(_) => Ok(()),
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("SetGroupBrightnessValue failed {:?}", e)
            ),
        }
    }

    pub async fn get_group_current_rgb_value(
        &self,
        index: u32,
    ) -> Result<Vec<SerializableRgb>, Error> {
        let tag = "LightFacade::get_group_current_rgb_value";
        let values = match self.get_proxy()?.get_group_current_rgb_value(index).await? {
            Ok(r) => match r {
                Some(v) => v,
                None => fx_err_and_bail!(&with_line!(tag), format_err!("Expected a vector")),
            },
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("GetGroupCurrentRgbValue failed {:?}", e)
            ),
        };
        let ret = values
            .into_iter()
            .map(|x| SerializableRgb { red: x.red, green: x.green, blue: x.blue })
            .collect::<Vec<_>>();
        Ok(ret)
    }

    pub async fn set_group_rgb_value(
        &self,
        index: u32,
        value: Vec<SerializableRgb>,
    ) -> Result<(), Error> {
        let tag = "LightFacade::set_group_rgb_value";
        let mut rgb = value
            .into_iter()
            .map(|x| Rgb { red: x.red, green: x.green, blue: x.blue })
            .collect::<Vec<Rgb>>();
        match self.get_proxy()?.set_group_rgb_value(index, &mut rgb.iter_mut()).await? {
            Ok(_) => Ok(()),
            Err(e) => {
                fx_err_and_bail!(&with_line!(tag), format_err!("SetGroupRgbValue failed {:?}", e))
            }
        }
    }
}
