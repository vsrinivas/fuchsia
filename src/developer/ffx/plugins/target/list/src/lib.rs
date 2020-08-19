// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::target_formatter::TargetFormatter,
    anyhow::{anyhow, Result},
    ffx_core::ffx_plugin,
    ffx_list_args::ListCommand,
    fidl_fuchsia_developer_bridge as bridge,
    std::convert::TryFrom,
    std::io::{stdout, Write},
};

mod target_formatter;

#[ffx_plugin()]
pub async fn list_targets(daemon_proxy: bridge::DaemonProxy, cmd: ListCommand) -> Result<()> {
    list_impl(daemon_proxy, cmd, Box::new(stdout())).await
}

async fn list_impl<W: Write>(
    daemon_proxy: bridge::DaemonProxy,
    cmd: ListCommand,
    mut writer: W,
) -> Result<()> {
    match daemon_proxy
        .list_targets(match cmd.nodename {
            Some(ref t) => t,
            None => "",
        })
        .await
    {
        Ok(r) => {
            match r.len() {
                0 => {
                    writeln!(writer, "No devices found.")?;
                }
                _ => {
                    let formatter = TargetFormatter::try_from(r)
                        .map_err(|e| anyhow!("target malformed: {:?}", e))?;
                    writeln!(writer, "{}", formatter.lines().join("\n"))?;
                }
            };
            Ok(())
        }
        Err(e) => {
            eprintln!("ERROR: {:?}", e);
            Err(anyhow!("Error listing targets: {:?}", e))
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        ffx_daemon::target::TargetAddr,
        fidl_fuchsia_developer_bridge::{
            DaemonRequest, RemoteControlState, Target as FidlTarget, TargetState, TargetType,
        },
        regex::Regex,
        std::io::BufWriter,
        std::net::IpAddr,
    };

    fn to_fidl_target(nodename: String) -> FidlTarget {
        let addr: TargetAddr =
            (IpAddr::from([0xfe80, 0x0, 0x0, 0x0, 0xdead, 0xbeef, 0xbeef, 0xbeef]), 3).into();
        FidlTarget {
            nodename: Some(nodename),
            addresses: Some(vec![addr.into()]),
            age_ms: Some(101),
            rcs_state: Some(RemoteControlState::Up),
            target_type: Some(TargetType::Unknown),
            target_state: Some(TargetState::Unknown),
        }
    }

    fn setup_fake_daemon_server(num_tests: usize) -> bridge::DaemonProxy {
        setup_fake_daemon_proxy(move |req| match req {
            DaemonRequest::ListTargets { value, responder } => {
                let fidl_values: Vec<FidlTarget> = if value.is_empty() {
                    (0..num_tests)
                        .map(|i| format!("Test {}", i))
                        .map(|name| to_fidl_target(name))
                        .collect()
                } else {
                    (0..num_tests)
                        .map(|i| format!("Test {}", i))
                        .filter(|t| *t == value)
                        .map(|name| to_fidl_target(name))
                        .collect()
                };
                responder.send(&mut fidl_values.into_iter().by_ref().take(512)).unwrap();
            }
            _ => assert!(false),
        })
    }

    async fn run_list_test(num_tests: usize, cmd: ListCommand) -> String {
        let mut output = String::new();
        let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let proxy = setup_fake_daemon_server(num_tests);
        let result = list_impl(proxy, cmd, writer).await.unwrap();
        assert_eq!(result, ());
        output
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_no_devices_and_no_nodename() -> Result<()> {
        let output = run_list_test(0, ListCommand { nodename: None }).await;
        assert_eq!("No devices found.\n".to_string(), output);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_one_device_and_no_nodename() -> Result<()> {
        let output = run_list_test(1, ListCommand { nodename: None }).await;
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
        let num_tests = 10;
        let output = run_list_test(num_tests, ListCommand { nodename: None }).await;
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
        let output = run_list_test(1, ListCommand { nodename: Some("Test 0".to_string()) }).await;
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
        let output = run_list_test(1, ListCommand { nodename: Some("blarg".to_string()) }).await;
        let value = format!("Test {}", 0);
        let node_listing = Regex::new(&value).expect("test regex");
        assert_eq!(0, node_listing.find_iter(&output).count());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_multiple_devices_and_not_matching_nodename() -> Result<()> {
        let num_tests = 25;
        let output =
            run_list_test(num_tests, ListCommand { nodename: Some("blarg".to_string()) }).await;
        for x in 0..num_tests {
            let value = format!("Test {}", x);
            let node_listing = Regex::new(&value).expect("test regex");
            assert_eq!(0, node_listing.find_iter(&output).count());
        }
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_with_multiple_devices_and_matching_nodename() -> Result<()> {
        let output = run_list_test(25, ListCommand { nodename: Some("Test 19".to_string()) }).await;
        let value = format!("Test {}", 0);
        let node_listing = Regex::new(&value).expect("test regex");
        assert_eq!(0, node_listing.find_iter(&output).count());
        let value = format!("Test {}", 19);
        let node_listing = Regex::new(&value).expect("test regex");
        assert_eq!(1, node_listing.find_iter(&output).count());
        Ok(())
    }
}
