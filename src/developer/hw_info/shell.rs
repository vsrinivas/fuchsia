// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(unused_crate_dependencies)]
use anyhow::{Context, Error};
use argh::FromArgs;
use core::fmt::Debug;
use fidl_fuchsia_hwinfo::{
    BoardInfo, BoardMarker, DeviceInfo, DeviceMarker, ProductInfo, ProductMarker,
};
use fuchsia_component::client::connect_to_protocol;
use futures as _;
use std::io::Write;

#[derive(Debug, Clone, Copy, PartialEq)]
enum HardwareInfo {
    DeviceInfo,
    ProductInfo,
    BoardInfo,
}

impl std::str::FromStr for HardwareInfo {
    type Err = String;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        match value {
            "device_info" => Ok(HardwareInfo::DeviceInfo),
            "product_info" => Ok(HardwareInfo::ProductInfo),
            "board_info" => Ok(HardwareInfo::BoardInfo),
            _ => Err(format!("Invalid key: {:?}.", value)),
        }
    }
}

fn unwrap_option<T: ToString>(info: Option<T>) -> String {
    info.map(|v| v.to_string()).unwrap_or("None".to_string())
}

fn unwrap_option_debug<T: Debug>(t: Option<T>) -> String {
    t.map(|t| format!("{:?}", t)).unwrap_or("None".to_string())
}

fn write_device_info<W: Write>(w: &mut W, device_info: DeviceInfo) -> Result<(), Error> {
    writeln!(w, "serial_number: {}", unwrap_option(device_info.serial_number))?;
    writeln!(w, "is_retail_demo: {}", unwrap_option(device_info.is_retail_demo))?;
    writeln!(w, "retail_sku: {}", unwrap_option(device_info.retail_sku))?;
    Ok(())
}

async fn print_device_info() -> Result<(), Error> {
    let device_provider = connect_to_protocol::<DeviceMarker>()
        .context("Failed to connect to device info service")?;
    let device_info = device_provider.get_info().await?;
    let mut w = std::io::stdout();
    write_device_info(&mut w, device_info).expect("Function write_device_info() failed");
    Ok(())
}

fn write_product_info<W: Write>(w: &mut W, product_info: ProductInfo) -> Result<(), Error> {
    writeln!(w, "sku: {}", unwrap_option(product_info.sku))?;
    writeln!(w, "language: {}", unwrap_option(product_info.language))?;
    writeln!(
        w,
        "country_code: {}",
        unwrap_option(product_info.regulatory_domain.and_then(|domain| domain.country_code))
    )?;
    writeln!(
        w,
        "locale_list[0]: {}",
        unwrap_option(
            product_info
                .locale_list
                .as_ref()
                .and_then(|locale_list| locale_list.get(0))
                .map(|locale| locale.id.clone())
        )
    )?;
    writeln!(w, "name: {}", unwrap_option(product_info.name))?;
    writeln!(w, "model: {}", unwrap_option(product_info.model))?;
    writeln!(w, "manufacturer: {}", unwrap_option(product_info.manufacturer))?;
    writeln!(w, "build_date: {}", unwrap_option(product_info.build_date))?;
    writeln!(w, "build_name: {}", unwrap_option(product_info.build_name))?;
    writeln!(w, "colorway: {}", unwrap_option(product_info.colorway))?;
    writeln!(w, "display: {}", unwrap_option(product_info.display))?;
    writeln!(w, "memory: {}", unwrap_option(product_info.memory))?;
    writeln!(w, "nand_storage: {}", unwrap_option(product_info.nand_storage))?;
    writeln!(w, "emmc_storage: {}", unwrap_option(product_info.emmc_storage))?;
    writeln!(w, "microphone: {}", unwrap_option(product_info.microphone))?;
    writeln!(w, "audio_amplifier: {}", unwrap_option(product_info.audio_amplifier))?;
    Ok(())
}

async fn print_product_info() -> Result<(), Error> {
    let product_provider = connect_to_protocol::<ProductMarker>()
        .context("Failed to connect to the product info service")?;
    let product_info = product_provider.get_info().await?;
    let mut w = std::io::stdout();
    write_product_info(&mut w, product_info).expect("Function write_product_info() failed");
    Ok(())
}

fn write_board_info<W: Write>(w: &mut W, board_info: BoardInfo) -> Result<(), Error> {
    writeln!(w, "name: {}", unwrap_option(board_info.name))?;
    writeln!(w, "revision: {}", unwrap_option(board_info.revision))?;
    writeln!(w, "cpu_architecture: {}", unwrap_option_debug(board_info.cpu_architecture))?;
    Ok(())
}

async fn print_board_info() -> Result<(), Error> {
    let board_provider = connect_to_protocol::<BoardMarker>()
        .context("Failed to connect to the board info service")?;
    let board_info = board_provider.get_info().await?;
    let mut w = std::io::stdout();
    write_board_info(&mut w, board_info).expect("Function write_board_info() failed");
    Ok(())
}

/// Hardware information command.
#[derive(Debug, PartialEq, FromArgs)]
struct BuildInfoCmd {
    /// valid keys: <device_info> <product_info> <board_info>
    #[argh(positional)]
    info: HardwareInfo,
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    let args: BuildInfoCmd = argh::from_env();

    match args.info {
        HardwareInfo::DeviceInfo => print_device_info().await?,
        HardwareInfo::ProductInfo => print_product_info().await?,
        HardwareInfo::BoardInfo => print_board_info().await?,
    }

    Ok(())
}

// =========== Testing the hw-info output ===================
#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_hwinfo::Architecture;
    use fidl_fuchsia_intl::{LocaleId, RegulatoryDomain};

    #[fuchsia::test]
    fn test_format_device_info() {
        // Tests with the DeviceInfo fields set to data.
        let device_info = DeviceInfo {
            serial_number: Some("526A348SJYW2319".to_string()),
            is_retail_demo: Some(false),
            retail_sku: Some("Number1".to_string()),
            ..DeviceInfo::EMPTY
        };
        let mut v = Vec::new();
        write_device_info(&mut v, device_info).expect("Testing write_device_info() failed");
        let write_str = String::from_utf8(v).expect("Failed to convert to string");
        let expect_str = concat!(
            "serial_number: 526A348SJYW2319\n",
            "is_retail_demo: false\n",
            "retail_sku: Number1\n"
        );

        // Verify expressions match
        assert_eq!(write_str, expect_str);

        //Tests with the DeviceInfo fields set to None.
        let device_info_none = DeviceInfo {
            serial_number: None,
            is_retail_demo: None,
            retail_sku: None,
            ..DeviceInfo::EMPTY
        };
        let mut v_none = Vec::new();
        write_device_info(&mut v_none, device_info_none)
            .expect("Testing write_device_info() failed");
        let write_str_none = String::from_utf8(v_none).expect("Failed to convert to string");
        let expect_str_none =
            concat!("serial_number: None\n", "is_retail_demo: None\n", "retail_sku: None\n");

        // Verify expression match
        assert_eq!(write_str_none, expect_str_none);
    }

    #[fuchsia::test]
    fn test_format_product_info() {
        // Tests with the ProductInfo fields set to data.
        let regulatory_domain_info =
            RegulatoryDomain { country_code: Some("CAN".to_string()), ..RegulatoryDomain::EMPTY };
        let locale_id_info = LocaleId { id: "10".to_string() };
        let product_info = ProductInfo {
            sku: Some("AB".to_string()),
            language: Some("SPA".to_string()),
            regulatory_domain: Some(regulatory_domain_info),
            locale_list: Some(vec![locale_id_info]),
            name: Some("testing-fuchsia".to_string()),
            model: Some("testing-model".to_string()),
            manufacturer: Some("testing-manufacturer".to_string()),
            build_date: Some("2022-10-20".to_string()),
            build_name: Some("build-test".to_string()),
            colorway: Some("colourway".to_string()),
            display: Some("glass".to_string()),
            memory: Some("64GB".to_string()),
            nand_storage: Some("flash".to_string()),
            emmc_storage: Some("memory".to_string()),
            microphone: Some("GTK2".to_string()),
            audio_amplifier: Some("Yes".to_string()),
            ..ProductInfo::EMPTY
        };
        let mut v = Vec::new();
        write_product_info(&mut v, product_info).expect("Testing write_product_info() failed");
        let write_str = String::from_utf8(v).expect("Failed to convert to string");
        let expect_str = concat!(
            "sku: AB\n",
            "language: SPA\n",
            "country_code: CAN\n",
            "locale_list[0]: 10\n",
            "name: testing-fuchsia\n",
            "model: testing-model\n",
            "manufacturer: testing-manufacturer\n",
            "build_date: 2022-10-20\n",
            "build_name: build-test\n",
            "colorway: colourway\n",
            "display: glass\n",
            "memory: 64GB\n",
            "nand_storage: flash\n",
            "emmc_storage: memory\n",
            "microphone: GTK2\n",
            "audio_amplifier: Yes\n"
        );

        // Verify expressions match
        assert_eq!(write_str, expect_str);

        // Tests with the ProductInfo fields set to None.
        let product_info_none = ProductInfo {
            sku: None,
            language: None,
            regulatory_domain: None,
            locale_list: None,
            name: None,
            model: None,
            manufacturer: None,
            build_date: None,
            build_name: None,
            colorway: None,
            display: None,
            memory: None,
            nand_storage: None,
            emmc_storage: None,
            microphone: None,
            audio_amplifier: None,
            ..ProductInfo::EMPTY
        };
        let mut v_none = Vec::new();
        write_product_info(&mut v_none, product_info_none)
            .expect("Testing write_product_info() failed");
        let write_str_none = String::from_utf8(v_none).expect("Failed to convert to string");
        let expect_str_none = concat!(
            "sku: None\n",
            "language: None\n",
            "country_code: None\n",
            "locale_list[0]: None\n",
            "name: None\n",
            "model: None\n",
            "manufacturer: None\n",
            "build_date: None\n",
            "build_name: None\n",
            "colorway: None\n",
            "display: None\n",
            "memory: None\n",
            "nand_storage: None\n",
            "emmc_storage: None\n",
            "microphone: None\n",
            "audio_amplifier: None\n"
        );

        // Verify expression match
        assert_eq!(write_str_none, expect_str_none);
    }

    #[fuchsia::test]
    fn test_format_board_info() {
        // Tests with the BoardInfo fields set to data.
        let board_info = BoardInfo {
            name: Some("skyrocket".to_string()),
            revision: Some("1000".to_string()),
            cpu_architecture: Some(Architecture::X64),
            ..BoardInfo::EMPTY
        };
        let mut v = Vec::new();
        write_board_info(&mut v, board_info).expect("Testing write_board_info() failed");
        let write_str = String::from_utf8(v).expect("Failed to convert to string");
        let expect_str =
            concat!("name: skyrocket\n", "revision: 1000\n", "cpu_architecture: X64\n");

        // Verify expressions match
        assert_eq!(write_str, expect_str);

        // Tests with the BoardInfo fields set to None.
        let board_info_none =
            BoardInfo { name: None, revision: None, cpu_architecture: None, ..BoardInfo::EMPTY };
        let mut v_none = Vec::new();
        write_board_info(&mut v_none, board_info_none).expect("Testing write_board_info() failed");
        let write_str_none = String::from_utf8(v_none).expect("Failed to convert to string");
        let expect_str_none =
            concat!("name: None\n", "revision: None\n", "cpu_architecture: None\n");

        // Verify expression match
        assert_eq!(write_str_none, expect_str_none);
    }
}
