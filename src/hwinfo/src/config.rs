// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_factory::MiscFactoryStoreProviderProxy,
    fidl_fuchsia_hwinfo,
    fidl_fuchsia_intl::{LocaleId, RegulatoryDomain},
    fidl_fuchsia_io::{DirectoryMarker, OPEN_RIGHT_READABLE},
    fuchsia_syslog::{self, fx_log_err, fx_log_warn},
    serde_derive::{Deserialize, Serialize},
    std::{fs::File, io, path::Path},
};

// CONFIG AND FACTORY FILE NAMES
const PRODUCT_CONFIG_JSON_FILE: &str = "/config/data/product_config.json";
const BOARD_CONFIG_JSON_FILE: &str = "/config/data/board_config.json";
const DEFAULT_PRODUCT_CONFIG_JSON_FILE: &str = "/config/data/default_product_config.json";
const DEFAULT_BOARD_CONFIG_JSON_FILE: &str = "/config/data/default_board_config.json";
const SERIAL_TXT: &str = "serial.txt";
const LOCALE_LIST_FILE: &str = "locale_list.txt";
const HW_TXT: &str = "hw.txt";
// CONFIG KEYS
const SKU_KEY: &str = "config";
const LANGUAGE_KEY: &str = "lang";
const REGULATORY_DOMAIN_KEY: &str = "country";

async fn read_factory_file(
    path: &str,
    proxy_handle: &MiscFactoryStoreProviderProxy,
) -> Result<String, Error> {
    let (dir_proxy, dir_server_end) = create_proxy::<DirectoryMarker>()?;
    proxy_handle.get_factory_store(dir_server_end)?;
    let file_proxy = io_util::open_file(&dir_proxy, &Path::new(path), OPEN_RIGHT_READABLE)?;
    let result = io_util::read_file(&file_proxy).await?.trim().to_owned();
    return Ok(result);
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct DeviceInfo {
    pub serial_number: Option<String>,
}

impl DeviceInfo {
    pub async fn load(proxy_handle: &MiscFactoryStoreProviderProxy) -> Self {
        let mut device_info = DeviceInfo { serial_number: None };
        device_info.serial_number = match read_factory_file(SERIAL_TXT, proxy_handle).await {
            Ok(content) => Some(content),
            Err(err) => {
                fx_log_err!("Failed to read factory file {}: {}", SERIAL_TXT.to_string(), err);
                None
            }
        };
        device_info
    }
}

impl Into<fidl_fuchsia_hwinfo::DeviceInfo> for DeviceInfo {
    fn into(self) -> fidl_fuchsia_hwinfo::DeviceInfo {
        fidl_fuchsia_hwinfo::DeviceInfo { serial_number: self.serial_number }
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct BoardInfo {
    pub name: Option<String>,
    pub revision: Option<String>,
}

impl BoardInfo {
    fn read_config(path: &str) -> Result<Self, Error> {
        let board_info: BoardInfo = serde_json::from_reader(io::BufReader::new(File::open(path)?))?;
        Ok(board_info)
    }

    pub fn load() -> Self {
        let board_info = BoardInfo::read_config(BOARD_CONFIG_JSON_FILE).unwrap_or_else(|err| {
            fx_log_err!("Failed to read board_config.json due to {}", err);
            BoardInfo::read_config(DEFAULT_BOARD_CONFIG_JSON_FILE).unwrap_or_else(|err| {
                fx_log_err!("Failed to read default_board_config.json due to {}", err);
                BoardInfo { name: None, revision: None }
            })
        });
        board_info
    }
}

impl Into<fidl_fuchsia_hwinfo::BoardInfo> for BoardInfo {
    fn into(self) -> fidl_fuchsia_hwinfo::BoardInfo {
        fidl_fuchsia_hwinfo::BoardInfo { name: self.name, revision: self.revision }
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct ConfigFile {
    pub name: String,
    pub model: String,
    pub manufacturer: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct ProductInfo {
    pub sku: Option<String>,
    pub language: Option<String>,
    pub country_code: Option<String>,
    pub locales: Vec<String>,
    pub name: Option<String>,
    pub model: Option<String>,
    pub manufacturer: Option<String>,
}

impl ProductInfo {
    fn new() -> Self {
        ProductInfo {
            sku: None,
            language: None,
            country_code: None,
            locales: Vec::new(),
            name: None,
            model: None,
            manufacturer: None,
        }
    }

    fn load_from_config_data(&mut self, path: &str) -> Result<(), Error> {
        let config_map: ConfigFile =
            serde_json::from_reader(io::BufReader::new(File::open(path)?))?;
        self.name = Some(config_map.name);
        self.model = Some(config_map.model);
        self.manufacturer = Some(config_map.manufacturer);
        Ok(())
    }

    async fn load_from_hw_file(
        &mut self,
        path: &str,
        proxy_handle: &MiscFactoryStoreProviderProxy,
    ) -> Result<(), Error> {
        let file_content = read_factory_file(path, proxy_handle).await?;
        for config in file_content.lines() {
            let pair: Vec<_> = config.splitn(2, "=").collect();
            let key = pair[0];
            let value = pair[1];
            match key {
                SKU_KEY => {
                    self.sku = Some(value.to_owned());
                }
                LANGUAGE_KEY => {
                    self.language = Some(value.to_owned());
                }
                REGULATORY_DOMAIN_KEY => {
                    self.country_code = Some(value.to_owned());
                }
                _ => {
                    fx_log_warn!("hw.txt dictionary values {} - {}", key, value.to_owned());
                }
            }
        }
        Ok(())
    }

    async fn load_from_locale_list(
        &mut self,
        path: &str,
        proxy_handle: &MiscFactoryStoreProviderProxy,
    ) -> Result<(), Error> {
        let file_content = read_factory_file(path, proxy_handle).await?;
        self.locales = Vec::new();
        for line in file_content.lines() {
            self.locales.push(line.trim().to_owned());
        }
        Ok(())
    }

    pub async fn load(proxy_handle: &MiscFactoryStoreProviderProxy) -> Self {
        let mut product_info = ProductInfo::new();
        if let Err(err) = product_info.load_from_config_data(PRODUCT_CONFIG_JSON_FILE) {
            fx_log_err!("Failed to load product_config.json due to {}", err);
            if let Err(err) = product_info.load_from_config_data(DEFAULT_PRODUCT_CONFIG_JSON_FILE) {
                fx_log_err!("Failed to load default_product_config.json due to {}", err);
            }
        }
        if let Err(err) = product_info.load_from_hw_file(HW_TXT, proxy_handle).await {
            fx_log_err!("Failed to load hw.txt.txt due to {}", err);
        }
        if let Err(err) = product_info.load_from_locale_list(LOCALE_LIST_FILE, proxy_handle).await {
            fx_log_err!("Failed to load local_list.txt due to {}", err);
        }
        product_info
    }
}

impl Into<fidl_fuchsia_hwinfo::ProductInfo> for ProductInfo {
    fn into(self) -> fidl_fuchsia_hwinfo::ProductInfo {
        let mut locale_list: Vec<LocaleId> = Vec::new();
        for locale in self.locales {
            locale_list.push(LocaleId { id: locale.to_owned() });
        }
        fidl_fuchsia_hwinfo::ProductInfo {
            sku: self.sku,
            language: self.language,
            regulatory_domain: if self.country_code.is_none() {
                None
            } else {
                Some(RegulatoryDomain { country_code: self.country_code })
            },
            locale_list: if locale_list.is_empty() { None } else { Some(locale_list) },
            name: self.name,
            model: self.model,
            manufacturer: self.manufacturer,
        }
    }
}
