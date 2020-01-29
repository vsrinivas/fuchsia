// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_hwinfo::{DeviceInfo, ProductInfo};
use serde_derive::Serialize;

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

        SerializableProductInfo {
            sku: product.sku.clone(),
            language: product.language.clone(),
            regulatory_domain: regulatory_domain,
            locale_list: locale_list,
            name: product.name.clone(),
            model: product.model.clone(),
            manufacturer: product.manufacturer.clone(),
        }
    }
}
