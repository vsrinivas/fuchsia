// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::target_formatter::{JsonTargetFormatter, TargetFormatter},
    anyhow::Result,
    errors::{ffx_bail, ffx_bail_with_code},
    ffx_core::ffx_plugin,
    ffx_list_args::{AddressTypes, ListCommand},
    ffx_writer::Writer,
    fidl_fuchsia_developer_ffx::{
        TargetCollectionProxy, TargetCollectionReaderMarker, TargetCollectionReaderRequest,
        TargetQuery,
    },
    futures::TryStreamExt,
    std::convert::TryFrom,
};

mod target_formatter;

fn address_types_from_cmd(cmd: &ListCommand) -> AddressTypes {
    if cmd.no_ipv4 && cmd.no_ipv6 {
        AddressTypes::None
    } else if cmd.no_ipv4 {
        AddressTypes::Ipv6Only
    } else if cmd.no_ipv6 {
        AddressTypes::Ipv4Only
    } else {
        AddressTypes::All
    }
}

#[ffx_plugin(TargetCollectionProxy = "daemon::protocol")]
pub async fn list_targets(
    tc_proxy: TargetCollectionProxy,
    #[ffx(machine = Vec<JsonTargets>)] writer: Writer,
    cmd: ListCommand,
) -> Result<()> {
    let (reader, server) = fidl::endpoints::create_endpoints::<TargetCollectionReaderMarker>()?;

    tc_proxy.list_targets(
        TargetQuery { string_matcher: cmd.nodename.clone(), ..TargetQuery::EMPTY },
        reader,
    )?;
    let mut res = Vec::new();
    let mut stream = server.into_stream()?;
    while let Ok(Some(TargetCollectionReaderRequest::Next { entry, responder })) =
        stream.try_next().await
    {
        responder.send()?;
        if entry.len() > 0 {
            res.extend(entry);
        } else {
            break;
        }
    }
    match res.len() {
        0 => {
            // Printed to stderr, so that if a user is parsing output, say from a formatted
            // output, that the message is not consumed. A stronger future strategy would
            // have richer behavior dependent upon whether the user has a controlling
            // terminal, which would require passing in more and richer IO delegates.
            if let Some(n) = cmd.nodename {
                ffx_bail_with_code!(2, "Device {} not found.", n);
            } else {
                writer.error("No devices found.")?;
            }
        }
        _ => {
            let address_types = address_types_from_cmd(&cmd);
            if let AddressTypes::None = address_types {
                ffx_bail!("Invalid arguments, cannot specify both --no_ipv4 and --no_ipv6")
            }
            if writer.is_machine() {
                let res = target_formatter::filter_targets_by_address_types(res, address_types);
                let mut formatter = JsonTargetFormatter::try_from(res)?;
                let default: Option<String> = ffx_config::get("target.default").await?;
                JsonTargetFormatter::set_default_target(&mut formatter.targets, default.as_deref());
                writer.machine(&formatter.targets)?;
            } else {
                let formatter =
                    Box::<dyn TargetFormatter>::try_from((cmd.format, address_types, res))?;
                let default: Option<String> = ffx_config::get("target.default").await?;
                writer.line(formatter.lines(default.as_deref()).join("\n"))?;
            }
        }
    };
    Ok(())
}

///////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        addr::TargetAddr,
        ffx_list_args::Format,
        fidl_fuchsia_developer_ffx as ffx,
        fidl_fuchsia_developer_ffx::{
            RemoteControlState, TargetInfo as FidlTargetInfo, TargetState, TargetType,
        },
        regex::Regex,
        std::net::IpAddr,
    };

    fn tab_list_cmd(nodename: Option<String>) -> ListCommand {
        ListCommand { nodename, format: Format::Tabular, ..Default::default() }
    }

    fn to_fidl_target(nodename: String) -> FidlTargetInfo {
        let addr: TargetAddr =
            (IpAddr::from([0xfe80, 0x0, 0x0, 0x0, 0xdead, 0xbeef, 0xbeef, 0xbeef]), 3).into();
        FidlTargetInfo {
            nodename: Some(nodename),
            addresses: Some(vec![addr.into()]),
            age_ms: Some(101),
            rcs_state: Some(RemoteControlState::Up),
            target_type: Some(TargetType::Unknown),
            target_state: Some(TargetState::Unknown),
            ..FidlTargetInfo::EMPTY
        }
    }

    fn setup_fake_target_collection_server(num_tests: usize) -> TargetCollectionProxy {
        setup_fake_tc_proxy(move |req| match req {
            ffx::TargetCollectionRequest::ListTargets { query, reader, .. } => {
                let reader = reader.into_proxy().unwrap();
                let fidl_values: Vec<FidlTargetInfo> =
                    if query.string_matcher.as_deref().map(|s| s.is_empty()).unwrap_or(true) {
                        (0..num_tests)
                            .map(|i| format!("Test {}", i))
                            .map(|name| to_fidl_target(name))
                            .collect()
                    } else {
                        let v = query.string_matcher.unwrap();
                        (0..num_tests)
                            .map(|i| format!("Test {}", i))
                            .filter(|t| *t == v)
                            .map(|name| to_fidl_target(name))
                            .collect()
                    };
                const CHUNK_SIZE: usize = 10;
                let mut iter = fidl_values.into_iter();
                fuchsia_async::Task::local(async move {
                    loop {
                        let next_chunk = iter.by_ref().take(CHUNK_SIZE);
                        let next_chunk_len = next_chunk.len();
                        reader.next(&mut next_chunk.collect::<Vec<_>>().into_iter()).await.unwrap();
                        if next_chunk_len == 0 {
                            break;
                        }
                    }
                })
                .detach();
            }
            r => panic!("unexpected request: {:?}", r),
        })
    }

    async fn try_run_list_test(num_tests: usize, cmd: ListCommand) -> Result<String> {
        let proxy = setup_fake_target_collection_server(num_tests);
        let writer = Writer::new_test(None);
        list_targets(proxy, writer.clone(), cmd).await?;
        writer.test_output()
    }

    async fn run_list_test(num_tests: usize, cmd: ListCommand) -> String {
        try_run_list_test(num_tests, cmd).await.unwrap()
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_no_devices_and_no_nodename() -> Result<()> {
        let _env = ffx_config::test_init().await.unwrap();
        let output = run_list_test(0, tab_list_cmd(None)).await;
        assert_eq!("".to_string(), output);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_one_device_and_no_nodename() -> Result<()> {
        let _env = ffx_config::test_init().await.unwrap();
        let output = run_list_test(1, tab_list_cmd(None)).await;
        let value = format!("Test {}", 0);
        let node_listing = Regex::new(&value).expect("test regex");
        assert_eq!(
            1,
            node_listing.find_iter(&output).count(),
            "could not find \"{}\" nodename in output:\n{}",
            value,
            output
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_multiple_devices_and_no_nodename() -> Result<()> {
        let _env = ffx_config::test_init().await.unwrap();
        let num_tests = 10;
        let output = run_list_test(num_tests, tab_list_cmd(None)).await;
        for x in 0..num_tests {
            let value = format!("Test {}", x);
            let node_listing = Regex::new(&value).expect("test regex");
            assert_eq!(
                1,
                node_listing.find_iter(&output).count(),
                "could not find \"{}\" nodename in output:\n{}",
                value,
                output
            );
        }
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_one_device_and_matching_nodename() -> Result<()> {
        let _env = ffx_config::test_init().await.unwrap();
        let output = run_list_test(1, tab_list_cmd(Some("Test 0".to_string()))).await;
        let value = format!("Test {}", 0);
        let node_listing = Regex::new(&value).expect("test regex");
        assert_eq!(
            1,
            node_listing.find_iter(&output).count(),
            "could not find \"{}\" nodename in output:\n{}",
            value,
            output
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_one_device_and_not_matching_nodename() -> Result<()> {
        let _env = ffx_config::test_init().await.unwrap();
        let output = try_run_list_test(1, tab_list_cmd(Some("blarg".to_string()))).await;
        assert!(output.is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_multiple_devices_and_not_matching_nodename() -> Result<()> {
        let _env = ffx_config::test_init().await.unwrap();
        let num_tests = 25;
        let output = try_run_list_test(num_tests, tab_list_cmd(Some("blarg".to_string()))).await;
        assert!(output.is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_multiple_devices_and_matching_nodename() -> Result<()> {
        let _env = ffx_config::test_init().await.unwrap();
        let output = run_list_test(25, tab_list_cmd(Some("Test 19".to_string()))).await;
        let value = format!("Test {}", 0);
        let node_listing = Regex::new(&value).expect("test regex");
        assert_eq!(0, node_listing.find_iter(&output).count());
        let value = format!("Test {}", 19);
        let node_listing = Regex::new(&value).expect("test regex");
        assert_eq!(1, node_listing.find_iter(&output).count());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_address_types_none() -> Result<()> {
        let _env = ffx_config::test_init().await.unwrap();
        let num_tests = 25;
        let cmd_none = ListCommand { no_ipv4: true, no_ipv6: true, ..Default::default() };
        let output = try_run_list_test(num_tests, cmd_none).await;
        assert!(output.is_err());
        Ok(())
    }

    #[test]
    fn test_address_types_from_cmd() -> Result<()> {
        let cmd_none = ListCommand { no_ipv4: true, no_ipv6: true, ..Default::default() };
        assert_eq!(address_types_from_cmd(&cmd_none), AddressTypes::None);
        let cmd_ipv4_only = ListCommand { no_ipv4: false, no_ipv6: true, ..Default::default() };
        assert_eq!(address_types_from_cmd(&cmd_ipv4_only), AddressTypes::Ipv4Only);
        let cmd_ipv6_only = ListCommand { no_ipv4: true, no_ipv6: false, ..Default::default() };
        assert_eq!(address_types_from_cmd(&cmd_ipv6_only), AddressTypes::Ipv6Only);
        let cmd_all = ListCommand { no_ipv4: false, no_ipv6: false, ..Default::default() };
        assert_eq!(address_types_from_cmd(&cmd_all), AddressTypes::All);
        let cmd_all_default = ListCommand::default();
        assert_eq!(address_types_from_cmd(&cmd_all_default), AddressTypes::All);
        Ok(())
    }
}
