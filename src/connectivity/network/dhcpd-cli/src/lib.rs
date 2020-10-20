// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    fuchsia_component::{client::AppBuilder, server::ServiceFs},
    futures::{FutureExt, StreamExt},
};

struct Command<'a> {
    args: Vec<&'a str>,
    expected_stdout: &'a str,
    expected_stderr: &'a str,
}

async fn test_cli_with_config(config: &'static str, commands: Vec<Command<'_>>) {
    let mut fs = ServiceFs::new_local();

    let mut netstack_builder =
        AppBuilder::new("fuchsia-pkg://fuchsia.com/dhcpd-cli-tests#meta/netstack-debug.cmx");
    let mut stash_builder =
        AppBuilder::new("fuchsia-pkg://fuchsia.com/dhcpd-cli-tests#meta/stash_secure.cmx");
    let mut dhcpd_builder =
        AppBuilder::new("fuchsia-pkg://fuchsia.com/dhcpd-testing#meta/dhcpd.cmx")
            .args(vec!["--config".to_string(), format!("/config/data/{}.json", config)]);

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

    let fs = fs.for_each_concurrent(None, |()| async move {});
    futures::pin_mut!(fs);

    let _stash = stash_builder.spawn(env.launcher()).expect("failed to launch test stash");
    let _dhcpd = dhcpd_builder.spawn(env.launcher()).expect("failed to launch test dhcpd");
    let _netstack = netstack_builder.spawn(env.launcher()).expect("failed to launch test netstack");

    for Command { args, expected_stdout, expected_stderr } in commands {
        let output =
            AppBuilder::new("fuchsia-pkg://fuchsia.com/dhcpd-cli-tests#meta/dhcpd-cli.cmx")
                .args(args)
                .output(env.launcher())
                .expect("failed to launch dhcpd-cli");

        let output = futures::select! {
            () = fs => panic!("request stream terminated"),
            output = output.fuse() => output.expect("dhcpd-cli terminated with error"),
        };
        let stdout = std::str::from_utf8(&output.stdout).expect("failed to get stdout");
        let stderr = std::str::from_utf8(&output.stderr).expect("failed to get stderr");
        assert_eq!(stderr, expected_stderr);
        assert_eq!(stdout, expected_stdout);
    }
}

async fn test_cli(commands: Vec<Command<'_>>) {
    test_cli_with_config("default_config", commands).await
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
        [
            Ipv4Address {
                addr: [
                    192,
                    168,
                    0,
                    1,
                ],
            },
        ],
    ),
    AddressPool(
        AddressPool {
            network_id: Some(
                Ipv4Address {
                    addr: [
                        192,
                        168,
                        0,
                        0,
                    ],
                },
            ),
            broadcast: Some(
                Ipv4Address {
                    addr: [
                        192,
                        168,
                        0,
                        128,
                    ],
                },
            ),
            mask: Some(
                Ipv4Address {
                    addr: [
                        255,
                        255,
                        255,
                        128,
                    ],
                },
            ),
            pool_range_start: Some(
                Ipv4Address {
                    addr: [
                        192,
                        168,
                        0,
                        0,
                    ],
                },
            ),
            pool_range_stop: Some(
                Ipv4Address {
                    addr: [
                        192,
                        168,
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
        "test_config",
        vec![Command { args: vec!["start"], expected_stdout: "", expected_stderr: "" }],
    )
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_stop() {
    test_cli(vec![Command { args: vec!["stop"], expected_stdout: "", expected_stderr: "" }]).await
}
