// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::Context as _,
    fuchsia_component::{client::AppBuilder, server::ServiceFs},
    futures::{FutureExt as _, StreamExt as _, TryStreamExt as _},
    net_declare::fidl_ip_v4,
};

struct Command<'a> {
    args: Vec<&'a str>,
    expected_stdout: &'a str,
    expected_stderr: &'a str,
}

async fn test_cli_with_config(
    parameters: &mut [fidl_fuchsia_net_dhcp::Parameter],
    commands: Vec<Command<'_>>,
) {
    let mut fs = ServiceFs::new_local();

    let mut netstack_builder =
        AppBuilder::new("fuchsia-pkg://fuchsia.com/dhcpd-cli-tests#meta/netstack-debug.cmx");
    let mut stash_builder =
        AppBuilder::new("fuchsia-pkg://fuchsia.com/dhcpd-cli-tests#meta/stash_secure.cmx");
    let mut dhcpd_builder =
        AppBuilder::new("fuchsia-pkg://fuchsia.com/dhcpd-cli-tests#meta/dhcpd.cmx");

    fs.add_proxy_service_to::<fidl_fuchsia_stash::SecureStoreMarker, _>(
        stash_builder
            .directory_request()
            .expect("failed to get test stash directory request")
            .clone(),
    )
    .add_proxy_service_to::<fidl_fuchsia_posix_socket::ProviderMarker, _>(
        netstack_builder
            .directory_request()
            .expect("failed to get test netstack directory request")
            .clone(),
    )
    .add_proxy_service_to::<fidl_fuchsia_net_dhcp::Server_Marker, _>(
        dhcpd_builder
            .directory_request()
            .expect("failed to get test dhcpd directory request")
            .clone(),
    );

    let env =
        fs.create_salted_nested_environment("test_cli").expect("failed to create environment");

    let fs = fs.for_each_concurrent(None, futures::future::ready);
    futures::pin_mut!(fs);

    let _stash = stash_builder.spawn(env.launcher()).expect("failed to launch test stash");
    let dhcpd = dhcpd_builder.spawn(env.launcher()).expect("failed to launch test dhcpd");
    let _netstack = netstack_builder.spawn(env.launcher()).expect("failed to launch test netstack");

    let dhcp_server = dhcpd
        .connect_to_service::<fidl_fuchsia_net_dhcp::Server_Marker>()
        .expect("failed to connect to DHCP server");
    let dhcp_server_ref = &dhcp_server;
    let test_fut = async {
        let () = futures::stream::iter(parameters.iter_mut())
            .map(Ok)
            .try_for_each_concurrent(None, |parameter| async move {
                dhcp_server_ref
                    .set_parameter(parameter)
                    .await
                    .context("failed to call dhcp/Server.SetParameter")?
                    .map_err(fuchsia_zircon::Status::from_raw)
                    .with_context(|| {
                        format!("dhcp/Server.SetParameter({:?}) returned error", parameter)
                    })
            })
            .await
            .expect("failed to configure DHCP server");

        for Command { args, expected_stdout, expected_stderr } in commands {
            let output =
                AppBuilder::new("fuchsia-pkg://fuchsia.com/dhcpd-cli-tests#meta/dhcpd-cli.cmx")
                    .args(args)
                    .output(env.launcher())
                    .expect("failed to launch dhcpd-cli")
                    .await
                    .expect("failed to collect dhcpd-cli output");

            let stdout = std::str::from_utf8(&output.stdout).expect("failed to get stdout");
            let stderr = std::str::from_utf8(&output.stderr).expect("failed to get stderr");
            assert_eq!(stderr, expected_stderr);
            assert_eq!(stdout, expected_stdout);
        }
    };

    futures::select! {
        () = fs => panic!("request stream terminated"),
        () = test_fut.fuse() => {},
    };
}

async fn test_cli(commands: Vec<Command<'_>>) {
    test_cli_with_config(&mut [], commands).await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_get_option_subnet() {
    test_cli(vec![Command {
        args: vec!["get", "option", "subnet-mask"],
        expected_stdout: "",
        expected_stderr: r#"Error: get_option(SubnetMask(SubnetMask { mask: None })) failed

Caused by:
    NOT_FOUND
"#,
    }])
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_get_parameter_lease() {
    test_cli(vec![Command {
        args: vec!["get", "parameter", "lease-length"],
        expected_stdout: r#"Lease(
    LeaseLength {
        default: Some(
            86400,
        ),
        max: Some(
            86400,
        ),
    },
)
"#,
        expected_stderr: "",
    }])
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_set_option_subnet() {
    test_cli(vec![Command {
        args: vec!["set", "option", "subnet-mask", "--mask", "255.255.255.0"],
        expected_stdout: "",
        expected_stderr: "",
    }])
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_set_parameter_lease() {
    test_cli(vec![Command {
        args: vec!["set", "parameter", "lease-length", "--default", "42"],
        expected_stdout: "",
        expected_stderr: "",
    }])
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_list_option() {
    test_cli(vec![
        Command {
            args: vec!["set", "option", "subnet-mask", "--mask", "255.255.255.0"],
            expected_stdout: "",
            expected_stderr: "",
        },
        Command {
            args: vec!["list", "option"],
            expected_stdout: r#"[
    SubnetMask(
        Ipv4Address {
            addr: [
                255,
                255,
                255,
                0,
            ],
        },
    ),
]
"#,
            expected_stderr: "",
        },
    ])
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_list_parameter() {
    test_cli(vec![Command {
        args: vec!["list", "parameter"],
        expected_stdout: r#"[
    IpAddrs(
        [],
    ),
    AddressPool(
        AddressPool {
            network_id: Some(
                Ipv4Address {
                    addr: [
                        0,
                        0,
                        0,
                        0,
                    ],
                },
            ),
            broadcast: Some(
                Ipv4Address {
                    addr: [
                        0,
                        0,
                        0,
                        0,
                    ],
                },
            ),
            mask: Some(
                Ipv4Address {
                    addr: [
                        0,
                        0,
                        0,
                        0,
                    ],
                },
            ),
            pool_range_start: Some(
                Ipv4Address {
                    addr: [
                        0,
                        0,
                        0,
                        0,
                    ],
                },
            ),
            pool_range_stop: Some(
                Ipv4Address {
                    addr: [
                        0,
                        0,
                        0,
                        0,
                    ],
                },
            ),
        },
    ),
    Lease(
        LeaseLength {
            default: Some(
                86400,
            ),
            max: Some(
                86400,
            ),
        },
    ),
    PermittedMacs(
        [],
    ),
    StaticallyAssignedAddrs(
        [],
    ),
    ArpProbe(
        false,
    ),
    BoundDeviceNames(
        [],
    ),
]
"#,
        expected_stderr: "",
    }])
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_reset_option() {
    test_cli(vec![
        Command {
            args: vec!["set", "option", "subnet-mask", "--mask", "255.255.255.0"],
            expected_stdout: "",
            expected_stderr: "",
        },
        Command {
            args: vec!["list", "option"],
            expected_stdout: r#"[
    SubnetMask(
        Ipv4Address {
            addr: [
                255,
                255,
                255,
                0,
            ],
        },
    ),
]
"#,
            expected_stderr: "",
        },
        Command { args: vec!["reset", "option"], expected_stdout: "", expected_stderr: "" },
        Command { args: vec!["list", "option"], expected_stdout: "[]\n", expected_stderr: "" },
    ])
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_reset_parameter() {
    test_cli(vec![
        Command {
            args: vec!["set", "parameter", "lease-length", "--default", "42"],
            expected_stdout: "",
            expected_stderr: "",
        },
        Command {
            args: vec!["get", "parameter", "lease-length"],
            expected_stdout: r#"Lease(
    LeaseLength {
        default: Some(
            42,
        ),
        max: Some(
            42,
        ),
    },
)
"#,
            expected_stderr: "",
        },
        Command { args: vec!["reset", "parameter"], expected_stdout: "", expected_stderr: "" },
        Command {
            args: vec!["get", "parameter", "lease-length"],
            expected_stdout: r#"Lease(
    LeaseLength {
        default: Some(
            86400,
        ),
        max: Some(
            86400,
        ),
    },
)
"#,
            expected_stderr: "",
        },
    ])
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_clear_leases() {
    test_cli(vec![Command { args: vec!["clear-leases"], expected_stdout: "", expected_stderr: "" }])
        .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_start_fails() {
    test_cli(vec![Command {
        args: vec!["start"],
        expected_stdout: "",
        // Starting the server fails because the default configuration has an
        // empty address pool.
        expected_stderr: "Error: failed to start server\n\nCaused by:\n    INVALID_ARGS\n",
    }])
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_start_succeeds() {
    test_cli_with_config(
        &mut [
            fidl_fuchsia_net_dhcp::Parameter::IpAddrs(vec![fidl_ip_v4!(192.168.0.1)]),
            fidl_fuchsia_net_dhcp::Parameter::AddressPool(fidl_fuchsia_net_dhcp::AddressPool {
                network_id: Some(fidl_ip_v4!(192.168.0.0)),
                broadcast: Some(fidl_ip_v4!(192.168.0.127)),
                mask: Some(fidl_ip_v4!(255.255.255.128)),
                pool_range_start: Some(fidl_ip_v4!(192.168.0.2)),
                pool_range_stop: Some(fidl_ip_v4!(192.168.0.5)),
            }),
        ],
        vec![Command { args: vec!["start"], expected_stdout: "", expected_stderr: "" }],
    )
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_stop() {
    test_cli(vec![Command { args: vec!["stop"], expected_stdout: "", expected_stderr: "" }]).await
}
