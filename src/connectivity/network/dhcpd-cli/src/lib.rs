// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;

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
    assert_eq!(stderr, "");
    assert_eq!(stdout, "SubnetMask(Ipv4Address { addr: [0, 0, 0, 0] })\n");
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
    assert_eq!(stdout, "Lease(LeaseLength { default: None, max: None })\n");
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
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_list_option() -> Result<(), Error> {
    let launcher = fuchsia_component::client::launcher()?;
    let cli_url = fuchsia_component::fuchsia_single_component_package_url!("dhcpd-cli");
    let cli_app = fuchsia_component::client::AppBuilder::new(cli_url).args(vec!["list", "option"]);
    let output = cli_app.output(&launcher)?.await?;
    let stdout = std::str::from_utf8(&output.stdout)?;
    let stderr = std::str::from_utf8(&output.stderr)?;
    assert_eq!(stderr, "");
    assert_eq!(stdout, "[]\n");
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
    assert_eq!(stdout, "[]\n");
    Ok(())
}
