// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;

#[fuchsia_async::run_singlethreaded(test)]
async fn test_get_option_subnet() -> Result<(), Error> {
    let launcher = fuchsia_component::client::launcher()?;
    let cli_url = fuchsia_component::fuchsia_single_component_package_url!("dhcpd-cli");
    let cli_app = fuchsia_component::client::AppBuilder::new(cli_url).args(vec![
        "get",
        "option",
        "subnet-mask",
    ]);
    let output = cli_app.output(&launcher)?.await?;
    let stdout = std::str::from_utf8(&output.stdout)?;
    let stderr = std::str::from_utf8(&output.stderr)?;
    assert_eq!(
        stderr,
        r#"Error: Status(NOT_FOUND)

get_option(SubnetMask(SubnetMask { mask: None })) failed
"#
    );
    assert_eq!(stdout, "");
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_get_parameter_lease() -> Result<(), Error> {
    let launcher = fuchsia_component::client::launcher()?;
    let cli_url = fuchsia_component::fuchsia_single_component_package_url!("dhcpd-cli");
    let cli_app = fuchsia_component::client::AppBuilder::new(cli_url).args(vec![
        "get",
        "parameter",
        "lease-length",
    ]);
    let output = cli_app.output(&launcher)?.await?;
    let stdout = std::str::from_utf8(&output.stdout)?;
    let stderr = std::str::from_utf8(&output.stderr)?;
    assert_eq!(stderr, "");
    assert_eq!(
        stdout,
        r#"Lease(
    LeaseLength {
        default: Some(
            86400,
        ),
        max: Some(
            86400,
        ),
    },
)
"#
    );
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_set_option_subnet() -> Result<(), Error> {
    let launcher = fuchsia_component::client::launcher()?;
    let cli_url = fuchsia_component::fuchsia_single_component_package_url!("dhcpd-cli");
    let cli_app = fuchsia_component::client::AppBuilder::new(cli_url).args(vec![
        "set",
        "option",
        "subnet-mask",
        "--mask",
        "255.255.255.0",
    ]);
    let output = cli_app.output(&launcher)?.await?;
    let stdout = std::str::from_utf8(&output.stdout)?;
    let stderr = std::str::from_utf8(&output.stderr)?;
    assert_eq!(stderr, "");
    assert_eq!(stdout, "");
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_set_parameter_lease() -> Result<(), Error> {
    let launcher = fuchsia_component::client::launcher()?;
    let cli_url = fuchsia_component::fuchsia_single_component_package_url!("dhcpd-cli");
    let cli_app = fuchsia_component::client::AppBuilder::new(cli_url).args(vec![
        "set",
        "parameter",
        "lease-length",
        "--default",
        "42",
    ]);
    let output = cli_app.output(&launcher)?.await?;
    let stdout = std::str::from_utf8(&output.stdout)?;
    let stderr = std::str::from_utf8(&output.stderr)?;
    assert_eq!(stderr, "");
    assert_eq!(stdout, "");

    // Return lease-length to its initial value to avoid polluting subsequent tests.
    let cli_url = fuchsia_component::fuchsia_single_component_package_url!("dhcpd-cli");
    let cli_app = fuchsia_component::client::AppBuilder::new(cli_url).args(vec![
        "set",
        "parameter",
        "lease-length",
        "--default",
        "86400",
        "--max",
        "86400",
    ]);
    let output = cli_app.output(&launcher)?.await?;
    let stdout = std::str::from_utf8(&output.stdout)?;
    let stderr = std::str::from_utf8(&output.stderr)?;
    assert_eq!(stderr, "");
    assert_eq!(stdout, "");

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_list_option() -> Result<(), Error> {
    let launcher = fuchsia_component::client::launcher()?;
    let cli_url = fuchsia_component::fuchsia_single_component_package_url!("dhcpd-cli");
    let cli_set_app = fuchsia_component::client::AppBuilder::new(cli_url).args(vec![
        "set",
        "option",
        "subnet-mask",
        "--mask",
        "255.255.255.0",
    ]);
    let set_output = cli_set_app.output(&launcher)?.await?;
    let set_stdout = std::str::from_utf8(&set_output.stdout)?;
    let set_stderr = std::str::from_utf8(&set_output.stderr)?;
    assert_eq!(set_stderr, "");
    assert_eq!(set_stdout, "");

    let cli_list_app =
        fuchsia_component::client::AppBuilder::new(cli_url).args(vec!["list", "option"]);
    let list_output = cli_list_app.output(&launcher)?.await?;
    let list_stdout = std::str::from_utf8(&list_output.stdout)?;
    let list_stderr = std::str::from_utf8(&list_output.stderr)?;
    assert_eq!(list_stderr, "");
    assert_eq!(
        list_stdout,
        r#"[
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
"#
    );
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_list_parameter() -> Result<(), Error> {
    let launcher = fuchsia_component::client::launcher()?;
    let cli_url = fuchsia_component::fuchsia_single_component_package_url!("dhcpd-cli");
    let cli_app =
        fuchsia_component::client::AppBuilder::new(cli_url).args(vec!["list", "parameter"]);
    let output = cli_app.output(&launcher)?.await?;
    let stdout = std::str::from_utf8(&output.stdout)?;
    let stderr = std::str::from_utf8(&output.stderr)?;
    assert_eq!(stderr, "");
    assert_eq!(
        stdout,
        r#"[
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
]
"#
    );
    Ok(())
}
