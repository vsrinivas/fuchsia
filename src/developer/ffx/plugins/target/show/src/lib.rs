// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::show::{ShowEntry, ShowValue},
    addr::TargetAddr,
    anyhow::{anyhow, bail, Result},
    ffx_core::ffx_plugin,
    ffx_target_show_args as args,
    fidl_fuchsia_boot::ArgumentsProxy,
    fidl_fuchsia_buildinfo::ProviderProxy,
    fidl_fuchsia_developer_ffx::{TargetAddrInfo, TargetProxy},
    fidl_fuchsia_feedback::{DeviceIdProviderProxy, LastRebootInfoProviderProxy},
    fidl_fuchsia_hwinfo::{Architecture, BoardProxy, DeviceProxy, ProductProxy},
    fidl_fuchsia_intl::RegulatoryDomain,
    fidl_fuchsia_update_channelcontrol::ChannelControlProxy,
    std::io::{stdout, Write},
    std::time::Duration,
    timeout::timeout,
};

mod show;

/// Main entry point for the `show` subcommand.
#[ffx_plugin(
    ChannelControlProxy = "core/system-update-checker:expose:fuchsia.update.channelcontrol.ChannelControl",
    ArgumentsProxy = "core/appmgr:out:fuchsia.boot.Arguments",
    BoardProxy = "core/hwinfo:expose:fuchsia.hwinfo.Board",
    DeviceProxy = "core/hwinfo:expose:fuchsia.hwinfo.Device",
    ProductProxy = "core/hwinfo:expose:fuchsia.hwinfo.Product",
    ProviderProxy = "core/build-info:expose:fuchsia.buildinfo.Provider",
    DeviceIdProviderProxy = "core/feedback_id:expose:fuchsia.feedback.DeviceIdProvider"
    LastRebootInfoProviderProxy = "core/feedback:expose:fuchsia.feedback.LastRebootInfoProvider"
)]
pub async fn show_cmd(
    channel_control_proxy: ChannelControlProxy,
    arguments_proxy: ArgumentsProxy,
    board_proxy: BoardProxy,
    device_proxy: DeviceProxy,
    product_proxy: ProductProxy,
    build_info_proxy: ProviderProxy,
    device_id_proxy: Option<DeviceIdProviderProxy>,
    last_reboot_info_proxy: LastRebootInfoProviderProxy,
    target_proxy: TargetProxy,
    target_show_args: args::TargetShow,
) -> Result<()> {
    show_cmd_impl(
        channel_control_proxy,
        arguments_proxy,
        board_proxy,
        device_proxy,
        product_proxy,
        build_info_proxy,
        device_id_proxy,
        last_reboot_info_proxy,
        target_proxy,
        target_show_args,
        &mut stdout(),
    )
    .await
}

// Implementation of the target show command.
async fn show_cmd_impl<W: Write>(
    channel_control_proxy: ChannelControlProxy,
    arguments_proxy: ArgumentsProxy,
    board_proxy: BoardProxy,
    device_proxy: DeviceProxy,
    product_proxy: ProductProxy,
    build_info_proxy: ProviderProxy,
    device_id_proxy: Option<DeviceIdProviderProxy>,
    last_reboot_info_proxy: LastRebootInfoProviderProxy,
    target_proxy: TargetProxy,
    target_show_args: args::TargetShow,
    writer: &mut W,
) -> Result<()> {
    if target_show_args.version {
        writeln!(writer, "ffx target show version 0.1")?;
        return Ok(());
    }
    // To add more show information, add a `gather_*_show(*) call to this
    // list, as well as the labels in the Ok() and vec![] just below.
    let show = match futures::try_join!(
        gather_target_show(target_proxy),
        gather_board_show(board_proxy),
        gather_device_show(device_proxy),
        gather_product_show(product_proxy),
        gather_update_show(channel_control_proxy),
        gather_build_info_show(build_info_proxy),
        gather_device_id_show(device_id_proxy),
        gather_last_reboot_info_show(last_reboot_info_proxy),
        gather_boot_params_show(arguments_proxy),
    ) {
        Ok((
            target,
            board,
            device,
            product,
            update,
            build,
            Some(device_id),
            reboot_info,
            boot_params,
        )) => {
            vec![target, board, device, product, update, build, device_id, reboot_info, boot_params]
        }
        Ok((target, board, device, product, update, build, None, reboot_info, boot_params)) => {
            vec![target, board, device, product, update, build, reboot_info, boot_params]
        }
        Err(e) => bail!(e),
    };
    if target_show_args.json {
        show::output_for_machine(&show, &target_show_args, writer)?;
    } else {
        show::output_for_human(&show, &target_show_args, writer)?;
    }
    Ok(())
}

/// Determine target information.
async fn gather_target_show(target_proxy: TargetProxy) -> Result<ShowEntry> {
    let host = target_proxy.identity().await?;
    let name = host.nodename;
    let addr_info = timeout(Duration::from_secs(1), target_proxy.get_ssh_address())
        .await?
        .map_err(|e| anyhow!("Failed to get ssh address: {:?}", e))?;
    let ifaces_str = {
        let addr = TargetAddr::from(&addr_info);
        let port = match addr_info {
            TargetAddrInfo::Ip(_info) => 22,
            TargetAddrInfo::IpPort(info) => info.port,
        };
        format!("{}:{}", addr, port)
    };
    Ok(ShowEntry::group(
        "Target",
        "target",
        "",
        vec![
            ShowEntry::str_value_with_highlight("Name", "name", "Target name.", &name),
            ShowEntry::str_value_with_highlight(
                "SSH Address",
                "ssh_address",
                "Interface address",
                &Some(ifaces_str),
            ),
        ],
    ))
}

/// Determine the device id for the target.
async fn gather_device_id_show(
    device_id: Option<DeviceIdProviderProxy>,
) -> Result<Option<ShowEntry>> {
    match device_id {
        None => Ok(None),
        Some(dev_id) => {
            let info = dev_id.get_id().await?;
            Ok(Some(ShowEntry::group(
                "Feedback",
                "feedback",
                "",
                vec![ShowEntry::str_value(
                    "Device ID",
                    "device_id",
                    "Feedback Device ID for target.",
                    &Some(info),
                )],
            )))
        }
    }
}

/// Determine the build info for the target.
async fn gather_build_info_show(build: ProviderProxy) -> Result<ShowEntry> {
    let info = build.get_build_info().await?;
    Ok(ShowEntry::group(
        "Build",
        "build",
        "",
        vec![
            ShowEntry::str_value("Version", "version", "Build version.", &info.version),
            ShowEntry::str_value("Product", "product", "Product config.", &info.product_config),
            ShowEntry::str_value("Board", "board", "Board config.", &info.board_config),
            ShowEntry::str_value(
                "Commit",
                "commit",
                "Integration Commit Date",
                &info.latest_commit_date,
            ),
        ],
    ))
}

/// Determine the boot params for the target.
async fn gather_boot_params_show(arguments: ArgumentsProxy) -> Result<ShowEntry> {
    let args = arguments.collect("").await?;
    let show_entries = args
        .iter()
        .map(|arg| {
            let arg_component: Vec<&str> = arg.splitn(2, '=').collect();
            let arg_value: Option<String> =
                if arg_component.len() > 1 { Some(arg_component[1].to_string()) } else { None };

            ShowEntry::str_value(arg_component[0], "", "", &arg_value)
        })
        .collect::<Vec<ShowEntry>>();
    Ok(ShowEntry::group("Boot Params", "boot", "", show_entries))
}

fn arch_to_string(arch: Option<Architecture>) -> Option<String> {
    match arch {
        Some(Architecture::X64) => Some("x64".to_string()),
        Some(Architecture::Arm64) => Some("arm64".to_string()),
        _ => None,
    }
}

/// Determine the device info for the device.
async fn gather_board_show(board: BoardProxy) -> Result<ShowEntry> {
    let info = board.get_info().await?;
    Ok(ShowEntry::group(
        "Board",
        "board",
        "",
        vec![
            ShowEntry::str_value("Name", "name", "SOC board name.", &info.name),
            ShowEntry::str_value("Revision", "revision", "SOC revision.", &info.revision),
            ShowEntry::str_value(
                "Instruction set",
                "instruction set",
                "Instruction set.",
                &arch_to_string(info.cpu_architecture),
            ),
        ],
    ))
}

/// Determine the device info for the device.
async fn gather_device_show(device: DeviceProxy) -> Result<ShowEntry> {
    let info = device.get_info().await?;
    Ok(ShowEntry::group(
        "Device",
        "device",
        "",
        vec![
            ShowEntry::str_value(
                "Serial number",
                "serial_number",
                "Unique ID for device.",
                &info.serial_number,
            ),
            ShowEntry::str_value(
                "Retail SKU",
                "retail_sku",
                "Stock Keeping Unit ID number.",
                &info.retail_sku,
            ),
            ShowEntry::bool_value(
                "Is retail demo",
                "is_retail_demo",
                "true if demonstration unit.",
                &info.is_retail_demo,
            ),
        ],
    ))
}

/// Determine the product info for the device.
async fn gather_product_show(product: ProductProxy) -> Result<ShowEntry> {
    let info = product.get_info().await?;

    let mut regulatory_domain =
        ShowEntry::new("Regulatory domain", "regulatory_domain", "Domain designation.");
    if let Some(RegulatoryDomain { country_code: Some(country_code), .. }) = info.regulatory_domain
    {
        regulatory_domain.value = Some(ShowValue::StringValue(country_code.to_string()));
    }

    let mut locale_list = ShowEntry::new("Locale list", "locale_list", "Locales supported.");
    if let Some(input) = info.locale_list {
        locale_list.value = Some(ShowValue::StringListValue(
            input.into_iter().map(|locale| locale.id.to_string()).collect(),
        ));
    }

    Ok(ShowEntry::group(
        "Product",
        "product",
        "",
        vec![
            ShowEntry::str_value(
                "Audio amplifier",
                "audio_amplifier",
                "Type of audio amp.",
                &info.audio_amplifier,
            ),
            ShowEntry::str_value(
                "Build date",
                "build_date",
                "When product was built.",
                &info.build_date,
            ),
            ShowEntry::str_value("Build name", "build_name", "Reference name.", &info.build_name),
            ShowEntry::str_value("Colorway", "colorway", "Colorway.", &info.colorway),
            ShowEntry::str_value("Display", "display", "Info about display.", &info.display),
            ShowEntry::str_value(
                "EMMC storage",
                "emmc_storage",
                "Size of storage.",
                &info.emmc_storage,
            ),
            ShowEntry::str_value("Language", "language", "language.", &info.language),
            regulatory_domain,
            locale_list,
            ShowEntry::str_value(
                "Manufacturer",
                "manufacturer",
                "Manufacturer of product.",
                &info.manufacturer,
            ),
            ShowEntry::str_value(
                "Microphone",
                "microphone",
                "Type of microphone.",
                &info.microphone,
            ),
            ShowEntry::str_value("Model", "model", "Model of the product.", &info.model),
            ShowEntry::str_value("Name", "name", "Name of the product.", &info.name),
            ShowEntry::str_value(
                "NAND storage",
                "nand_storage",
                "Size of storage.",
                &info.nand_storage,
            ),
            ShowEntry::str_value("Memory", "memory", "Amount of RAM.", &info.memory),
            ShowEntry::str_value("SKU", "sku", "SOC board name.", &info.sku),
        ],
    ))
}

/// Determine the update show of the device, including update channels.
async fn gather_update_show(channel_control: ChannelControlProxy) -> Result<ShowEntry> {
    let current_channel = channel_control.get_current().await?;
    let next_channel = channel_control.get_target().await?;
    Ok(ShowEntry::group(
        "Update",
        "update",
        "",
        vec![
            ShowEntry::str_value(
                "Current channel",
                "current_channel",
                "Channel that is currently in use.",
                &Some(current_channel),
            ),
            ShowEntry::str_value(
                "Next channel",
                "next_channel",
                "Channel used for the next update.",
                &Some(next_channel),
            ),
        ],
    ))
}

/// Show information about the last device_reboot.
async fn gather_last_reboot_info_show(
    last_reboot_info_proxy: LastRebootInfoProviderProxy,
) -> Result<ShowEntry> {
    let info = last_reboot_info_proxy.get().await?;
    let graceful = info.graceful.map(|x| {
        ShowEntry::str_value(
            "Graceful",
            "graceful",
            "Whether the last reboot happened in a controlled manner.",
            &Some(if x { "true" } else { "false" }.to_owned()),
        )
    });
    let reason = info.reason.map(|x| {
        ShowEntry::str_value(
            "Reason",
            "reason",
            "Reason for the last reboot.",
            &Some(format!("{:?}", x)),
        )
    });
    let uptime = info.uptime.map(|x| {
        ShowEntry::str_value(
            "Uptime (ns)",
            "uptime",
            "How long the device was running prior to the last reboot.",
            &Some(x.to_string()),
        )
    });
    Ok(ShowEntry::group(
        "Last Reboot",
        "last_reboot",
        "",
        vec![graceful, reason, uptime].into_iter().filter_map(|x| x).collect(),
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_boot::ArgumentsRequest;
    use fidl_fuchsia_buildinfo::{BuildInfo, ProviderRequest};
    use fidl_fuchsia_developer_ffx::{TargetAddrInfo, TargetInfo, TargetIp, TargetRequest};
    use fidl_fuchsia_feedback::{
        DeviceIdProviderRequest, LastReboot, LastRebootInfoProviderRequest, RebootReason,
    };
    use fidl_fuchsia_hwinfo::{
        Architecture, BoardInfo, BoardRequest, DeviceInfo, DeviceRequest, ProductInfo,
        ProductRequest,
    };
    use fidl_fuchsia_net::{IpAddress, Ipv4Address};
    use fidl_fuchsia_update_channelcontrol::ChannelControlRequest;
    use serde_json::Value;

    const IPV4_ADDR: [u8; 4] = [127, 0, 0, 1];

    const TEST_OUTPUT_HUMAN: &'static str = "\
        Target: \
        \n    Name: \u{1b}[38;5;2m\"fake_fuchsia_device\"\u{1b}[m\
        \n    SSH Address: \u{1b}[38;5;2m\"127.0.0.1:22\"\u{1b}[m\
        \nBoard: \
        \n    Name: \"fake_name\"\
        \n    Revision: \"fake_revision\"\
        \n    Instruction set: \"x64\"\
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
        \nBuild: \
        \n    Version: \"fake_version\"\
        \n    Product: \"fake_product\"\
        \n    Board: \"fake_board\"\
        \n    Commit: \"fake_commit\"\
        \nFeedback: \
        \n    Device ID: \"fake_device_id\"\
        \nLast Reboot: \
        \n    Graceful: \"true\"\
        \n    Reason: \"ZbiSwap\"\
        \n    Uptime (ns): \"65000\"\
        \nBoot Params: \
        \n    fake_boot_key: \"fake_boot_value\"\
        \n    fake_valueless_boot_key: \
        \n";

    fn setup_fake_target_server() -> TargetProxy {
        setup_fake_target_proxy(move |req| match req {
            TargetRequest::GetSshAddress { responder, .. } => {
                responder
                    .send(&mut TargetAddrInfo::Ip(TargetIp {
                        ip: IpAddress::Ipv4(Ipv4Address { addr: IPV4_ADDR }),
                        scope_id: 1,
                    }))
                    .expect("fake ssh address");
            }
            TargetRequest::Identity { responder, .. } => {
                let addrs = vec![TargetAddrInfo::Ip(TargetIp {
                    ip: IpAddress::Ipv4(Ipv4Address { addr: IPV4_ADDR }),
                    scope_id: 1,
                })];
                let nodename = Some("fake_fuchsia_device".to_string());
                responder
                    .send(TargetInfo { nodename, addresses: Some(addrs), ..TargetInfo::EMPTY })
                    .unwrap();
            }
            _ => assert!(false),
        })
    }

    fn setup_fake_device_id_server() -> DeviceIdProviderProxy {
        setup_fake_device_id_proxy(move |req| match req {
            DeviceIdProviderRequest::GetId { responder } => {
                responder.send("fake_device_id").unwrap();
            }
        })
    }

    fn setup_fake_build_info_server() -> ProviderProxy {
        setup_fake_build_info_proxy(move |req| match req {
            ProviderRequest::GetBuildInfo { responder } => {
                responder
                    .send(BuildInfo {
                        version: Some("fake_version".to_string()),
                        product_config: Some("fake_product".to_string()),
                        board_config: Some("fake_board".to_string()),
                        latest_commit_date: Some("fake_commit".to_string()),
                        ..BuildInfo::EMPTY
                    })
                    .unwrap();
            }
        })
    }

    fn setup_fake_board_server() -> BoardProxy {
        setup_fake_board_proxy(move |req| match req {
            BoardRequest::GetInfo { responder } => {
                responder
                    .send(BoardInfo {
                        name: Some("fake_name".to_string()),
                        revision: Some("fake_revision".to_string()),
                        cpu_architecture: Some(Architecture::X64),
                        ..BoardInfo::EMPTY
                    })
                    .unwrap();
            }
        })
    }

    fn setup_fake_last_reboot_info_server() -> LastRebootInfoProviderProxy {
        setup_fake_last_reboot_info_proxy(move |req| match req {
            LastRebootInfoProviderRequest::Get { responder } => {
                responder
                    .send(LastReboot {
                        graceful: Some(true),
                        uptime: Some(65000),
                        reason: Some(RebootReason::ZbiSwap),
                        ..LastReboot::EMPTY
                    })
                    .unwrap();
            }
        })
    }

    fn setup_fake_arguments_server() -> ArgumentsProxy {
        setup_fake_arguments_proxy(move |req| match req {
            ArgumentsRequest::Collect { responder, .. } => {
                let x = vec!["fake_boot_key=fake_boot_value", "fake_valueless_boot_key"];

                responder.send(&mut x.into_iter()).unwrap();
            }
            _ => {}
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_show_cmd_impl() {
        let mut output = Vec::new();
        show_cmd_impl(
            setup_fake_channel_control_server(),
            setup_fake_arguments_server(),
            setup_fake_board_server(),
            setup_fake_device_server(),
            setup_fake_product_server(),
            setup_fake_build_info_server(),
            Some(setup_fake_device_id_server()),
            setup_fake_last_reboot_info_server(),
            setup_fake_target_server(),
            args::TargetShow::default(),
            &mut output,
        )
        .await
        .expect("show_cmd_impl");
        // Convert to a readable string instead of using a byte string and comparing that. Unless
        // you can read u8 arrays well, this helps debug the output.
        let readable = String::from_utf8(output).expect("output is utf-8");
        assert_eq!(readable, TEST_OUTPUT_HUMAN);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_show_cmd_impl_json() {
        let mut output = Vec::new();
        show_cmd_impl(
            setup_fake_channel_control_server(),
            setup_fake_arguments_server(),
            setup_fake_board_server(),
            setup_fake_device_server(),
            setup_fake_product_server(),
            setup_fake_build_info_server(),
            Some(setup_fake_device_id_server()),
            setup_fake_last_reboot_info_server(),
            setup_fake_target_server(),
            args::TargetShow { json: true, ..Default::default() },
            &mut output,
        )
        .await
        .expect("show_cmd_impl");
        let v: Value =
            serde_json::from_str(std::str::from_utf8(&output).unwrap()).expect("Valid JSON");
        assert!(v.is_array());
        assert_eq!(v.as_array().unwrap().len(), 9);

        assert_eq!(v[0]["label"], Value::String("target".to_string()));
        assert_eq!(v[1]["label"], Value::String("board".to_string()));
        assert_eq!(v[2]["label"], Value::String("device".to_string()));
        assert_eq!(v[3]["label"], Value::String("product".to_string()));
        assert_eq!(v[4]["label"], Value::String("update".to_string()));
        assert_eq!(v[5]["label"], Value::String("build".to_string()));
        assert_eq!(v[6]["label"], Value::String("feedback".to_string()));
        assert_eq!(v[7]["label"], Value::String("last_reboot".to_string()));
        assert_eq!(v[8]["label"], Value::String("boot".to_string()));

        assert_eq!(v[0]["child"].as_array().unwrap().len(), 2);
        assert_eq!(v[1]["child"].as_array().unwrap().len(), 3);
        assert_eq!(v[2]["child"].as_array().unwrap().len(), 3);
        assert_eq!(v[3]["child"].as_array().unwrap().len(), 16);
        assert_eq!(v[4]["child"].as_array().unwrap().len(), 2);
        assert_eq!(v[5]["child"].as_array().unwrap().len(), 4);
        assert_eq!(v[6]["child"].as_array().unwrap().len(), 1);
        assert_eq!(v[7]["child"].as_array().unwrap().len(), 3);
        assert_eq!(v[8]["child"].as_array().unwrap().len(), 2);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gather_board_show() {
        let test_proxy = setup_fake_board_server();
        let result = gather_board_show(test_proxy).await.expect("gather board show");
        assert_eq!(result.title, "Board");
        assert_eq!(result.child[0].title, "Name");
        assert_eq!(result.child[0].value, Some(ShowValue::StringValue("fake_name".to_string())));
        assert_eq!(result.child[1].title, "Revision");
        assert_eq!(
            result.child[1].value,
            Some(ShowValue::StringValue("fake_revision".to_string()))
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
                        ..DeviceInfo::EMPTY
                    })
                    .unwrap();
            }
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gather_device_show() {
        let test_proxy = setup_fake_device_server();
        let result = gather_device_show(test_proxy).await.expect("gather device show");
        assert_eq!(result.title, "Device");
        assert_eq!(result.child[0].title, "Serial number");
        assert_eq!(result.child[0].value, Some(ShowValue::StringValue("fake_serial".to_string())));
        assert_eq!(result.child[1].title, "Retail SKU");
        assert_eq!(result.child[1].value, Some(ShowValue::StringValue("fake_sku".to_string())));
        assert_eq!(result.child[2].title, "Is retail demo");
        assert_eq!(result.child[2].value, Some(ShowValue::BoolValue(false)))
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
                            ..RegulatoryDomain::EMPTY
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
                        ..ProductInfo::EMPTY
                    })
                    .unwrap();
            }
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gather_product_show() {
        let test_proxy = setup_fake_product_server();
        let result = gather_product_show(test_proxy).await.expect("gather product show");
        assert_eq!(result.title, "Product");
        assert_eq!(result.child[0].title, "Audio amplifier");
        assert_eq!(
            result.child[0].value,
            Some(ShowValue::StringValue("fake_audio_amplifier".to_string()))
        );
        assert_eq!(result.child[1].title, "Build date");
        assert_eq!(
            result.child[1].value,
            Some(ShowValue::StringValue("fake_build_date".to_string()))
        );
        assert_eq!(result.child[2].title, "Build name");
        assert_eq!(
            result.child[2].value,
            Some(ShowValue::StringValue("fake_build_name".to_string()))
        );
        assert_eq!(result.child[3].title, "Colorway");
        assert_eq!(
            result.child[3].value,
            Some(ShowValue::StringValue("fake_colorway".to_string()))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gather_last_reboot_info_show() {
        let test_proxy = setup_fake_last_reboot_info_server();
        let result =
            gather_last_reboot_info_show(test_proxy).await.expect("gather last reboot info show");
        assert_eq!(result.title, "Last Reboot");
        assert_eq!(result.child[0].title, "Graceful");
        assert_eq!(result.child[0].value, Some(ShowValue::StringValue("true".to_string())));
        assert_eq!(result.child[1].title, "Reason");
        assert_eq!(result.child[1].value, Some(ShowValue::StringValue("ZbiSwap".to_string())));
        assert_eq!(result.child[2].title, "Uptime (ns)");
        assert_eq!(result.child[2].value, Some(ShowValue::StringValue("65000".to_string())));
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
    async fn test_gather_update_show() {
        let test_proxy = setup_fake_channel_control_server();
        let result = gather_update_show(test_proxy).await.expect("gather update show");
        assert_eq!(result.title, "Update");
        assert_eq!(result.child[0].title, "Current channel");
        assert_eq!(result.child[0].value, Some(ShowValue::StringValue("fake_channel".to_string())));
        assert_eq!(result.child[1].title, "Next channel");
        assert_eq!(result.child[1].value, Some(ShowValue::StringValue("fake_target".to_string())));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_arch_to_string() {
        assert_eq!(arch_to_string(Some(Architecture::X64)), Some("x64".to_string()));
        assert_eq!(arch_to_string(Some(Architecture::Arm64)), Some("arm64".to_string()));
        assert_eq!(arch_to_string(None), None);
    }
}
