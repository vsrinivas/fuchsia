// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net_ext as fidl_net_ext;
use fuchsia_async;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

use crate::tests::{assert_ping, TestSetup};

trait TestAddr {
    const ALICE_IP: fidl_net_ext::IpAddress;
    const BOB_IP: fidl_net_ext::IpAddress;
    const SUBNET_ADDR: fidl_net_ext::IpAddress;
    const SUBNET_PREFIX: u8;
}

struct TestIpv4Addr;
impl TestAddr for TestIpv4Addr {
    const ALICE_IP: fidl_net_ext::IpAddress =
        fidl_net_ext::IpAddress(IpAddr::V4(Ipv4Addr::new(192, 168, 0, 1)));
    const BOB_IP: fidl_net_ext::IpAddress =
        fidl_net_ext::IpAddress(IpAddr::V4(Ipv4Addr::new(192, 168, 0, 2)));
    const SUBNET_ADDR: fidl_net_ext::IpAddress =
        fidl_net_ext::IpAddress(IpAddr::V4(Ipv4Addr::new(192, 168, 0, 0)));
    const SUBNET_PREFIX: u8 = 24;
}

// TODO(http://fxbug.dev/53896): remove allow(dead_code) when IPv6 ping3 test is re-enabled.
#[allow(dead_code)]
struct TestIpv6Addr;
impl TestAddr for TestIpv6Addr {
    const ALICE_IP: fidl_net_ext::IpAddress =
        fidl_net_ext::IpAddress(IpAddr::V6(Ipv6Addr::new(2, 0, 0, 0, 0, 0, 0, 1)));
    const BOB_IP: fidl_net_ext::IpAddress =
        fidl_net_ext::IpAddress(IpAddr::V6(Ipv6Addr::new(2, 0, 0, 0, 0, 0, 0, 2)));
    const SUBNET_ADDR: fidl_net_ext::IpAddress =
        fidl_net_ext::IpAddress(IpAddr::V6(Ipv6Addr::new(2, 0, 0, 0, 0, 0, 0, 0)));
    const SUBNET_PREFIX: u8 = 64;
}

async fn alice_to_bob_over_proto<A: TestAddr>(args: &str, expected_return_code: i64) {
    let mut t = TestSetup::new(A::SUBNET_ADDR.into(), A::SUBNET_PREFIX).await.unwrap();

    let alice = t.add_env("alice", A::ALICE_IP.into()).await.unwrap();
    let _bob = t.add_env("bob", A::BOB_IP.into()).await.unwrap();

    let full_args = format!("{} -v {}", A::BOB_IP, args);
    assert_ping(&alice, &full_args, expected_return_code).await;
}

async fn alice_to_bob(args: &str, expected_return_code: i64) {
    alice_to_bob_over_proto::<TestIpv4Addr>(args, expected_return_code).await;
    // TODO(http://fxbug.dev/53896): fix and reneable IPv6 ping3 test.
    #[cfg(issue_53896)]
    alice_to_bob_over_proto::<TestIpv6Addr>(args, expected_return_code).await;
}

// Send a simple ping between two clients: Alice and Bob
#[fuchsia_async::run_singlethreaded(test)]
async fn can_ping() {
    alice_to_bob("-c 1", 0).await;
}

// Send a ping with insufficent payload size for a timestamp.
#[fuchsia_async::run_singlethreaded(test)]
async fn can_ping_without_timestamp() {
    alice_to_bob("-c 1 -s 0", 0).await;
}

// Send pings until a timeout. Exit with code 0.
#[fuchsia_async::run_singlethreaded(test)]
async fn can_timeout() {
    alice_to_bob("-w 5", 0).await;
}

// Send a set amount of pings that times out before completion. Exit with code 1.
#[fuchsia_async::run_singlethreaded(test)]
async fn not_enough_pings_before_timeout() {
    alice_to_bob("-c 1000 -w 5", 1).await;
}

// Specify an invalid count argument. Exit with code 2.
#[fuchsia_async::run_singlethreaded(test)]
async fn invalid_count() {
    alice_to_bob("-c 0", 2).await;
}

// Specify an invalid deadline argument. Exit with code 2.
#[fuchsia_async::run_singlethreaded(test)]
async fn invalid_deadline() {
    alice_to_bob("-w 0", 2).await;
}

// Specify an invalid interval argument. Exit with code 2.
#[fuchsia_async::run_singlethreaded(test)]
async fn invalid_interval() {
    alice_to_bob("-i 0", 2).await;
}
