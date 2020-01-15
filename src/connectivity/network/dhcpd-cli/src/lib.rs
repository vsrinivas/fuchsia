// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::{FutureExt, StreamExt};

#[cfg(test)]
async fn test_cli(args: Vec<&str>, expected_stdout: &str) {
    let mut fs = fuchsia_component::server::ServiceFs::new_local();
    fs.add_proxy_service::<fidl_fuchsia_stash::StoreMarker, _>()
        .add_component_proxy_service::<fidl_fuchsia_net_dhcp::Server_Marker>(
        fuchsia_component::fuchsia_single_component_package_url!("dhcpd").to_string(),
        Some(vec!["--test".to_string()]),
    );
    let env =
        fs.create_salted_nested_environment("test_cli").expect("failed to create environment");
    let output = fuchsia_component::client::AppBuilder::new(
        fuchsia_component::fuchsia_single_component_package_url!("dhcpd-cli"),
    )
    .args(args)
    .output(env.launcher())
    .expect("failed to launch dhcpd-cli");
    let fs = fs.for_each_concurrent(None, |_incoming| async {});

    let output = futures::select! {
        () = fs.fuse() => panic!("request stream terminated"),
        output = output.fuse() => output.expect("dhcpd-cli terminated with error"),
    };
    let stdout = std::str::from_utf8(&output.stdout).expect("failed to get stdout");
    let stderr = std::str::from_utf8(&output.stderr).expect("failed to get stderr");
    assert_eq!(stderr, "");
    assert_eq!(stdout, expected_stdout);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_get_option_subnet() {
    test_cli(
        vec!["get", "option", "subnet-mask"],
        "SubnetMask(Ipv4Address { addr: [0, 0, 0, 0] })\n",
    )
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_get_parameter_lease() {
    test_cli(
        vec!["get", "parameter", "lease-length"],
        "Lease(LeaseLength { default: None, max: None })\n",
    )
    .await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_set_option_subnet() {
    test_cli(vec!["set", "option", "subnet-mask", "--mask", "255.255.255.0"], "").await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_set_parameter_lease() {
    test_cli(vec!["set", "parameter", "lease-length", "--default", "42"], "").await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_list_option() {
    test_cli(vec!["list", "option"], "[]\n").await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_list_parameter() {
    test_cli(vec!["list", "parameter"], "[]\n").await
}
