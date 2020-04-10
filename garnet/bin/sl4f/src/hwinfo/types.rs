// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_hwinfo::{BoardInfo, DeviceInfo, ProductInfo};
use serde::Serialize;

#[derive(Clone, Debug, Serialize)]
pub struct SerializableDeviceInfo {
    pub serial_number: Option<String>,
}

/// DeviceInfo object is not serializable so serialize the object.
impl SerializableDeviceInfo {
    pub fn new(device: &DeviceInfo) -> Self {
        SerializableDeviceInfo { serial_number: device.serial_number.clone() }
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct SerializableProductInfo {
    pub sku: Option<String>,
    pub language: Option<String>,
    pub regulatory_domain: Option<String>,
    pub locale_list: Option<Vec<String>>,
    pub name: Option<String>,
    pub model: Option<String>,
    pub manufacturer: Option<String>,
    pub build_date: Option<String>,
    pub build_name: Option<String>,
    pub colorway: Option<String>,
    pub display: Option<String>,
    pub memory: Option<String>,
    pub nand_storage: Option<String>,
    pub emmc_storage: Option<String>,
    pub microphone: Option<String>,
    pub audio_amplifier: Option<String>,
}

/// ProductInfo object is not serializable so serialize the object.
impl SerializableProductInfo {
    pub fn new(product: &ProductInfo) -> Self {
        let regulatory_domain = match &product.regulatory_domain {
            Some(r) => r.country_code.clone(),
            None => None,
        };

        let locale_list = match &product.locale_list {
            Some(list) => {
                let mut locale_id_list = Vec::new();
                for locale in list.into_iter() {
                    locale_id_list.push(locale.id.to_string());
                }
                Some(locale_id_list)
            }
            None => None,
        };

        // the syntax of build_date is "2019-10-24T04:23:49", we want it to be "191024"
        let build_date = match &product.build_date {
            Some(date) => {
                let sub = &date[2..10];
                let result = sub.replace("-", "");
                Some(result.to_string())
            }
            None => None,
        };

        SerializableProductInfo {
            sku: product.sku.clone(),
            language: product.language.clone(),
            regulatory_domain: regulatory_domain,
            locale_list: locale_list,
            name: product.name.clone(),
            model: product.model.clone(),
            manufacturer: product.manufacturer.clone(),
            build_date: build_date,
            build_name: product.build_name.clone(),
            colorway: product.colorway.clone(),
            display: product.display.clone(),
            memory: product.memory.clone(),
            nand_storage: product.nand_storage.clone(),
            emmc_storage: product.emmc_storage.clone(),
            microphone: product.microphone.clone(),
            audio_amplifier: product.audio_amplifier.clone(),
        }
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct SerializableBoardInfo {
    pub name: Option<String>,
    pub revision: Option<String>,
}

/// Board object is not serializable so serialize the object.
impl SerializableBoardInfo {
    pub fn new(board: &BoardInfo) -> Self {
        SerializableBoardInfo { name: board.name.clone(), revision: board.revision.clone() }
    }
}
