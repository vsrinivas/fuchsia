// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_ext as fnet_ext;

use anyhow::Context as _;

use crate::tests::{create_environments, ping3, EnvConfig};

trait TestAddr {
    const ALICE_IP: fnet_ext::IpAddress;
    const BOB_IP: fnet_ext::IpAddress;
    const SUBNET_ADDR: fnet_ext::IpAddress;
    const SUBNET_PREFIX: u8;
}

struct TestIpv4Addr;
impl TestAddr for TestIpv4Addr {
    const ALICE_IP: fnet_ext::IpAddress =
        fnet_ext::IpAddress(IpAddr::V4(Ipv4Addr::new(192, 168, 0, 1)));
    const BOB_IP: fnet_ext::IpAddress =
        fnet_ext::IpAddress(IpAddr::V4(Ipv4Addr::new(192, 168, 0, 2)));
    const SUBNET_ADDR: fnet_ext::IpAddress =
        fnet_ext::IpAddress(IpAddr::V4(Ipv4Addr::new(192, 168, 0, 0)));
    const SUBNET_PREFIX: u8 = 24;
}

struct TestIpv6Addr;
impl TestAddr for TestIpv6Addr {
    const ALICE_IP: fnet_ext::IpAddress =
        fnet_ext::IpAddress(IpAddr::V6(Ipv6Addr::new(2, 0, 0, 0, 0, 0, 0, 1)));
    const BOB_IP: fnet_ext::IpAddress =
        fnet_ext::IpAddress(IpAddr::V6(Ipv6Addr::new(2, 0, 0, 0, 0, 0, 0, 2)));
    const SUBNET_ADDR: fnet_ext::IpAddress =
        fnet_ext::IpAddress(IpAddr::V6(Ipv6Addr::new(2, 0, 0, 0, 0, 0, 0, 0)));
    const SUBNET_PREFIX: u8 = 64;
}

async fn alice_to_bob_over_proto<A: TestAddr>(
    name: String,
    args: &str,
    expected_return_code: i64,
) -> Result<(), anyhow::Error> {
    let sandbox = netemul::TestSandbox::new().context("new sandbox")?;
    let (envs, _net) = create_environments(
        &sandbox,
        name,
        fnet::Subnet { addr: A::SUBNET_ADDR.into(), prefix_len: A::SUBNET_PREFIX },
        vec![
            EnvConfig {
                name: "alice".to_string(),
                static_ip: fnet::Subnet { addr: A::ALICE_IP.into(), prefix_len: A::SUBNET_PREFIX },
            },
            EnvConfig {
                name: "bob".to_string(),
                static_ip: fnet::Subnet { addr: A::BOB_IP.into(), prefix_len: A::SUBNET_PREFIX },
            },
        ],
    )
    .await
    .context("create environments")?;
    let (alice_env, _alice_iface) = &envs[0];

    let full_args = format!("{} -v {}", A::BOB_IP, args);
    let exit_status = ping3(&alice_env, &full_args).await.context("ping3")?;
    if exit_status.code() == expected_return_code {
        Ok(())
    } else {
        Err(anyhow::anyhow!(
            "ping3 exited with status = {:?}, expected return code = {}",
            exit_status,
            expected_return_code
        ))
    }
}

async fn alice_to_bob(
    name: &str,
    args: &str,
    expected_return_code: i64,
) -> Result<(), anyhow::Error> {
    let () =
        alice_to_bob_over_proto::<TestIpv4Addr>(format!("{}_v4", name), args, expected_return_code)
            .await
            .context("ipv4")?;

    let () =
        alice_to_bob_over_proto::<TestIpv6Addr>(format!("{}_v6", name), args, expected_return_code)
            .await
            .context("ipv6")?;

    Ok(())
}

// Send a simple ping between two clients: Alice and Bob.
#[fuchsia_async::run_singlethreaded(test)]
async fn can_ping() -> Result<(), anyhow::Error> {
    alice_to_bob("can_ping", "-c 1", 0).await
}

// Send a ping with insufficient payload size for a timestamp.
#[fuchsia_async::run_singlethreaded(test)]
async fn can_ping_without_timestamp() -> Result<(), anyhow::Error> {
    alice_to_bob("can_ping_without_timestamp", "-c 1 -s 0", 0).await
}

// Send pings until a timeout. Exit with code 0.
#[fuchsia_async::run_singlethreaded(test)]
async fn can_timeout() -> Result<(), anyhow::Error> {
    alice_to_bob("can_timeout", "-w 5", 0).await
}

// Send a set amount of pings that times out before completion. Exit with code 1.
#[fuchsia_async::run_singlethreaded(test)]
async fn not_enough_pings_before_timeout() -> Result<(), anyhow::Error> {
    alice_to_bob("not_enough_pings_before_timeout", "-c 1000 -w 5", 1).await
}

// Specify an invalid count argument. Exit with code 2.
#[fuchsia_async::run_singlethreaded(test)]
async fn invalid_count() -> Result<(), anyhow::Error> {
    alice_to_bob("invalid_count", "-c 0", 2).await
}

// Specify an invalid deadline argument. Exit with code 2.
#[fuchsia_async::run_singlethreaded(test)]
async fn invalid_deadline() -> Result<(), anyhow::Error> {
    alice_to_bob("invalid_deadline", "-w 0", 2).await
}

// Specify an invalid interval argument. Exit with code 2.
#[fuchsia_async::run_singlethreaded(test)]
async fn invalid_interval() -> Result<(), anyhow::Error> {
    alice_to_bob("invalid_interval", "-i 0", 2).await
}
