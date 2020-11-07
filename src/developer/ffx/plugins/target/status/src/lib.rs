// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::status::{StatusEntry, StatusValue},
    anyhow::{bail, Result},
    ffx_core::ffx_plugin,
    ffx_target_status_args as args,
    fidl_fuchsia_hwinfo::{BoardProxy, DeviceProxy, ProductProxy},
    fidl_fuchsia_intl::RegulatoryDomain,
    fidl_fuchsia_update_channelcontrol::ChannelControlProxy,
    std::io::{stdout, Write},
};

mod status;

/// Main entry point for the `status` subcommand.
#[ffx_plugin(
    ChannelControlProxy = "core/appmgr:out:fuchsia.update.channelcontrol.ChannelControl",
    BoardProxy = "core/appmgr:out:fuchsia.hwinfo.Board",
    DeviceProxy = "core/appmgr:out:fuchsia.hwinfo.Device",
    ProductProxy = "core/appmgr:out:fuchsia.hwinfo.Product"
)]
pub async fn status_cmd(
    channel_control_proxy: ChannelControlProxy,
    board_proxy: BoardProxy,
    device_proxy: DeviceProxy,
    product_proxy: ProductProxy,
    target_status_args: args::TargetStatus,
) -> Result<()> {
    status_cmd_impl(
        channel_control_proxy,
        board_proxy,
        device_proxy,
        product_proxy,
        target_status_args,
        Box::new(stdout()),
    )
    .await
}

// Implementation of the target status command.
async fn status_cmd_impl<W: Write>(
    channel_control_proxy: ChannelControlProxy,
    board_proxy: BoardProxy,
    device_proxy: DeviceProxy,
    product_proxy: ProductProxy,
    target_status_args: args::TargetStatus,
    mut writer: W,
) -> Result<()> {
    if target_status_args.version {
        println!("ffx target status version 0.1");
        return Ok(());
    }
    // To add more status information, add a `gather_*_status(*) call to this
    // list, as well as the labels in the Ok() and vec![] just below.
    let status = match futures::try_join!(
        gather_board_status(board_proxy),
        gather_device_status(device_proxy),
        gather_product_status(product_proxy),
        gather_update_status(channel_control_proxy)
    ) {
        Ok((board, device, product, update)) => vec![board, device, product, update],
        Err(e) => bail!(e),
    };
    if target_status_args.json {
        status::output_for_machine(&status, &target_status_args, &mut writer)?;
    } else {
        status::output_for_human(&status, &target_status_args, &mut writer)?;
    }
    Ok(())
}

/// Determine the device info for the device.
async fn gather_board_status(board: BoardProxy) -> Result<StatusEntry> {
    let info = board.get_info().await?;
    Ok(StatusEntry::group(
        "Board",
        "board",
        "",
        vec![
            StatusEntry::str_value("Name", "name", "SOC board name.", &info.name),
            StatusEntry::str_value("Revision", "revision", "SOC revision.", &info.revision),
        ],
    ))
}

/// Determine the device info for the device.
async fn gather_device_status(device: DeviceProxy) -> Result<StatusEntry> {
    let info = device.get_info().await?;
    Ok(StatusEntry::group(
        "Device",
        "device",
        "",
        vec![
            StatusEntry::str_value(
                "Serial number",
                "serial_number",
                "Unique ID for device.",
                &info.serial_number,
            ),
            StatusEntry::str_value(
                "Retail SKU",
                "retail_sku",
                "Stock Keeping Unit ID number.",
                &info.retail_sku,
            ),
            StatusEntry::bool_value(
                "Is retail demo",
                "is_retail_demo",
                "true if demonstration unit.",
                &info.is_retail_demo,
            ),
        ],
    ))
}

/// Determine the product info for the device.
async fn gather_product_status(product: ProductProxy) -> Result<StatusEntry> {
    let info = product.get_info().await?;

    let mut regulatory_domain =
        StatusEntry::new("Regulatory domain", "regulatory_domain", "Domain designation.");
    if let Some(RegulatoryDomain { country_code: Some(country_code) }) = info.regulatory_domain {
        regulatory_domain.value = Some(StatusValue::StringValue(country_code.to_string()));
    }

    let mut locale_list = StatusEntry::new("Locale list", "locale_list", "Locales supported.");
    if let Some(input) = info.locale_list {
        locale_list.value = Some(StatusValue::StringListValue(
            input.into_iter().map(|locale| locale.id.to_string()).collect(),
        ));
    }

    Ok(StatusEntry::group(
        "Product",
        "product",
        "",
        vec![
            StatusEntry::str_value(
                "Audio amplifier",
                "audio_amplifier",
                "Type of audio amp.",
                &info.audio_amplifier,
            ),
            StatusEntry::str_value(
                "Build date",
                "build_date",
                "When product was built.",
                &info.build_date,
            ),
            StatusEntry::str_value("Build name", "build_name", "Reference name.", &info.build_name),
            StatusEntry::str_value("Colorway", "colorway", "Colorway.", &info.colorway),
            StatusEntry::str_value("Display", "display", "Info about display.", &info.display),
            StatusEntry::str_value(
                "EMMC storage",
                "emmc_storage",
                "Size of storage.",
                &info.emmc_storage,
            ),
            StatusEntry::str_value("Language", "language", "language.", &info.language),
            regulatory_domain,
            locale_list,
            StatusEntry::str_value(
                "Manufacturer",
                "manufacturer",
                "Manufacturer of product.",
                &info.manufacturer,
            ),
            StatusEntry::str_value(
                "Microphone",
                "microphone",
                "Type of microphone.",
                &info.microphone,
            ),
            StatusEntry::str_value("Model", "model", "Model of the product.", &info.model),
            StatusEntry::str_value("Name", "name", "Name of the product.", &info.name),
            StatusEntry::str_value(
                "NAND storage",
                "nand_storage",
                "Size of storage.",
                &info.nand_storage,
            ),
            StatusEntry::str_value("Memory", "memory", "Amount of RAM.", &info.memory),
            StatusEntry::str_value("SKU", "sku", "SOC board name.", &info.sku),
        ],
    ))
}

/// Determine the update status of the device, including update channels.
async fn gather_update_status(channel_control: ChannelControlProxy) -> Result<StatusEntry> {
    let current_channel = channel_control.get_current().await?;
    let next_channel = channel_control.get_target().await?;
    Ok(StatusEntry::group(
        "Update",
        "update",
        "",
        vec![
            StatusEntry::str_value(
                "Current channel",
                "current_channel",
                "Channel that is currently in use.",
                &Some(current_channel),
            ),
            StatusEntry::str_value(
                "Next channel",
                "next_channel",
                "Channel used for the next update.",
                &Some(next_channel),
            ),
        ],
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_hwinfo::{
        BoardInfo, BoardRequest, DeviceInfo, DeviceRequest, ProductInfo, ProductRequest,
    };
    use fidl_fuchsia_update_channelcontrol::ChannelControlRequest;
    use serde_json::Value;

    const TEST_OUTPUT_HUMAN: &'static [u8] = b"\
        Board: \
        \n    Name: \"fake_name\"\
        \n    Revision: \"fake_revision\"\
        \nDevice: \
        \n    Serial number: \"fake_serial\"\
        \n    Retail SKU: \"fake_sku\"\
        \n    Is retail demo: false\
        \nProduct: \
        \n    Audio amplifier: \"fake_audio_amplifier\"\
        \n    Build date: \"fake_build_date\"\
        \n    Build name: \"fake_build_name\"\
        \n    Colorway: \"fake_colorway\"\
        \n    Display: \"fake_display\"\
        \n    EMMC storage: \"fake_emmc_storage\"\
        \n    Language: \"fake_language\"\
        \n    Regulatory domain: \"fake_regulatory_domain\"\
        \n    Locale list: []\
        \n    Manufacturer: \"fake_manufacturer\"\
        \n    Microphone: \"fake_microphone\"\
        \n    Model: \"fake_model\"\
        \n    Name: \"fake_name\"\
        \n    NAND storage: \"fake_nand_storage\"\
        \n    Memory: \"fake_memory\"\
        \n    SKU: \"fake_sku\"\
        \nUpdate: \
        \n    Current channel: \"fake_channel\"\
        \n    Next channel: \"fake_target\"\
        \n";

    fn setup_fake_board_server() -> BoardProxy {
        setup_fake_board_proxy(move |req| match req {
            BoardRequest::GetInfo { responder } => {
                responder
                    .send(BoardInfo {
                        name: Some("fake_name".to_string()),
                        revision: Some("fake_revision".to_string()),
                    })
                    .unwrap();
            }
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_status_cmd_impl() {
        let mut output = Vec::new();
        status_cmd_impl(
            setup_fake_channel_control_server(),
            setup_fake_board_server(),
            setup_fake_device_server(),
            setup_fake_product_server(),
            args::TargetStatus::default(),
            &mut output,
        )
        .await
        .expect("status_cmd_impl");
        assert_eq!(output, TEST_OUTPUT_HUMAN);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_status_cmd_impl_json() {
        let mut output = Vec::new();
        status_cmd_impl(
            setup_fake_channel_control_server(),
            setup_fake_board_server(),
            setup_fake_device_server(),
            setup_fake_product_server(),
            args::TargetStatus { json: true, ..Default::default() },
            &mut output,
        )
        .await
        .expect("status_cmd_impl");
        let v: Value =
            serde_json::from_str(std::str::from_utf8(&output).unwrap()).expect("Valid JSON");
        assert!(v.is_array());
        assert_eq!(v.as_array().unwrap().len(), 4);

        assert_eq!(v[0]["label"], Value::String("board".to_string()));
        assert_eq!(v[1]["label"], Value::String("device".to_string()));
        assert_eq!(v[2]["label"], Value::String("product".to_string()));
        assert_eq!(v[3]["label"], Value::String("update".to_string()));

        assert_eq!(v[0]["child"].as_array().unwrap().len(), 2);
        assert_eq!(v[1]["child"].as_array().unwrap().len(), 3);
        assert_eq!(v[2]["child"].as_array().unwrap().len(), 16);
        assert_eq!(v[3]["child"].as_array().unwrap().len(), 2);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gather_board_status() {
        let test_proxy = setup_fake_board_server();
        let result = gather_board_status(test_proxy).await.expect("gather board status");
        assert_eq!(result.title, "Board");
        assert_eq!(result.child[0].title, "Name");
        assert_eq!(result.child[0].value, Some(StatusValue::StringValue("fake_name".to_string())));
        assert_eq!(result.child[1].title, "Revision");
        assert_eq!(
            result.child[1].value,
            Some(StatusValue::StringValue("fake_revision".to_string()))
        );
    }

    fn setup_fake_device_server() -> DeviceProxy {
        setup_fake_device_proxy(move |req| match req {
            DeviceRequest::GetInfo { responder } => {
                responder
                    .send(DeviceInfo {
                        serial_number: Some("fake_serial".to_string()),
                        is_retail_demo: Some(false),
                        retail_sku: Some("fake_sku".to_string()),
                    })
                    .unwrap();
            }
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gather_device_status() {
        let test_proxy = setup_fake_device_server();
        let result = gather_device_status(test_proxy).await.expect("gather device status");
        assert_eq!(result.title, "Device");
        assert_eq!(result.child[0].title, "Serial number");
        assert_eq!(
            result.child[0].value,
            Some(StatusValue::StringValue("fake_serial".to_string()))
        );
        assert_eq!(result.child[1].title, "Retail SKU");
        assert_eq!(result.child[1].value, Some(StatusValue::StringValue("fake_sku".to_string())));
        assert_eq!(result.child[2].title, "Is retail demo");
        assert_eq!(result.child[2].value, Some(StatusValue::BoolValue(false)))
    }

    fn setup_fake_product_server() -> ProductProxy {
        setup_fake_product_proxy(move |req| match req {
            ProductRequest::GetInfo { responder } => {
                responder
                    .send(ProductInfo {
                        sku: Some("fake_sku".to_string()),
                        language: Some("fake_language".to_string()),
                        regulatory_domain: Some(RegulatoryDomain {
                            country_code: Some("fake_regulatory_domain".to_string()),
                        }),
                        locale_list: Some(vec![]),
                        name: Some("fake_name".to_string()),
                        audio_amplifier: Some("fake_audio_amplifier".to_string()),
                        build_date: Some("fake_build_date".to_string()),
                        build_name: Some("fake_build_name".to_string()),
                        colorway: Some("fake_colorway".to_string()),
                        display: Some("fake_display".to_string()),
                        emmc_storage: Some("fake_emmc_storage".to_string()),
                        manufacturer: Some("fake_manufacturer".to_string()),
                        memory: Some("fake_memory".to_string()),
                        microphone: Some("fake_microphone".to_string()),
                        model: Some("fake_model".to_string()),
                        nand_storage: Some("fake_nand_storage".to_string()),
                    })
                    .unwrap();
            }
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gather_product_status() {
        let test_proxy = setup_fake_product_server();
        let result = gather_product_status(test_proxy).await.expect("gather device status");
        assert_eq!(result.title, "Product");
        assert_eq!(result.child[0].title, "Audio amplifier");
        assert_eq!(
            result.child[0].value,
            Some(StatusValue::StringValue("fake_audio_amplifier".to_string()))
        );
        assert_eq!(result.child[1].title, "Build date");
        assert_eq!(
            result.child[1].value,
            Some(StatusValue::StringValue("fake_build_date".to_string()))
        );
        assert_eq!(result.child[2].title, "Build name");
        assert_eq!(
            result.child[2].value,
            Some(StatusValue::StringValue("fake_build_name".to_string()))
        );
        assert_eq!(result.child[3].title, "Colorway");
        assert_eq!(
            result.child[3].value,
            Some(StatusValue::StringValue("fake_colorway".to_string()))
        );
    }

    fn setup_fake_channel_control_server() -> ChannelControlProxy {
        setup_fake_channel_control_proxy(move |req| match req {
            ChannelControlRequest::GetCurrent { responder } => {
                responder.send("fake_channel").unwrap();
            }
            ChannelControlRequest::GetTarget { responder } => {
                responder.send("fake_target").unwrap();
            }
            _ => assert!(false),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gather_update_status() {
        let test_proxy = setup_fake_channel_control_server();
        let result = gather_update_status(test_proxy).await.expect("gather update status");
        assert_eq!(result.title, "Update");
        assert_eq!(result.child[0].title, "Current channel");
        assert_eq!(
            result.child[0].value,
            Some(StatusValue::StringValue("fake_channel".to_string()))
        );
        assert_eq!(result.child[1].title, "Next channel");
        assert_eq!(
            result.child[1].value,
            Some(StatusValue::StringValue("fake_target".to_string()))
        );
    }
}
