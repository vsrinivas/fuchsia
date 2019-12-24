// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Unit tests of the helper library. To prevent cross-talk, each unit test that sends packets on
//! an endpoint is given its own network and set of endpoints.

#![cfg(test)]

use {
    super::*,
    anyhow::Error,
    fidl_fuchsia_netemul_network::{EndpointManagerMarker, NetworkContextMarker},
    fuchsia_async::{self as fasync, Executor},
    net_types::ip::IpVersion,
    std::net::UdpSocket,
};

#[fasync::run_singlethreaded(test)]
async fn args_test() {
    // Assign to help inferring specialization of into.
    let mut argsv: Vec<String> = Args::new("path").into();
    assert_eq!(argsv, vec!["path".into()] as Vec<String>);

    argsv = Args::new("path").insert_arg("-a".into()).into();
    assert_eq!(argsv, vec!["-a".into(), "path".into()] as Vec<String>);

    argsv = Args::new("path").insert_arg("-a".into()).insert_arg("-b".into()).into();
    assert_eq!(argsv, vec!["-a".into(), "-b".into(), "path".into()] as Vec<String>);
}

#[fasync::run_singlethreaded(test)]
async fn check_all_substrs_test() {
    assert!(check_all_substrs(
        "the quick brown fox jumps over the lazy dog",
        &[
            "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q",
            "r", "s", "t", "u", "v", "w", "x", "y", "z"
        ]
    )
    .is_ok());

    assert!(check_all_substrs("", &[]).is_ok());
    assert!(check_all_substrs("test", &[]).is_ok());

    assert_eq!(check_all_substrs("", &["a"]).unwrap_err(), "a");
    assert_eq!(
        check_all_substrs("aaabbbcccddd", &["ab", "bbc", "cdd", "abbc", "ddd"]).unwrap_err(),
        "abbc"
    );
}

// The helper test configuration facet should have set up everything needed to create a
// `TestEnvironment` successfully.
#[test]
fn new_test_environment_endpoints_test() -> Result<(), Error> {
    let mut test_env = TestEnvironment::new(Executor::new().unwrap(), "helper_udp")?;
    let ep_tx = test_env.endpoint(EndpointType::TX)?;
    let ep_rx = test_env.endpoint(EndpointType::RX)?;

    // A fake endpoint should be return successfully, but it has no name and not managed
    // by `EndpointManager`.
    let _ep_fake = test_env.create_fake_endpoint()?;

    let netctx = client::connect_to_service::<NetworkContextMarker>()?;
    let (epm, epm_server_end) = fidl::endpoints::create_proxy::<EndpointManagerMarker>()?;
    netctx.get_endpoint_manager(epm_server_end)?;

    let test = async {
        let ep_list = epm.list_endpoints().await?;

        assert_eq!(ep_tx.get_name().await?, "helper_udp_tx");
        assert_eq!(ep_rx.get_name().await?, "helper_udp_rx");
        assert!(ep_list.contains(&"helper_udp_tx".into()));
        assert!(ep_list.contains(&"helper_udp_rx".into()));

        Ok(())
    };

    let mut executor: Executor = test_env.into();
    executor.run_singlethreaded(test)
}

#[test]
fn new_test_environment_bad_name_test() {
    assert!(TestEnvironment::new(Executor::new().unwrap(), "bad_name").is_err());
}

#[fasync::run_singlethreaded(test)]
async fn send_udp_packets_test() -> Result<(), Error> {
    const READ_TIMEOUT: std::time::Duration = std::time::Duration::from_millis(30);
    let mut buf: [u8; 256] = [0; 256];

    let addr_tx = EndpointType::TX.default_socket_addr(IpVersion::V4);
    let addr_rx = EndpointType::RX.default_socket_addr(IpVersion::V4);
    let socket_tx = UdpSocket::bind(addr_tx)?;
    let socket_rx = UdpSocket::bind(addr_rx)?;
    socket_rx.set_read_timeout(Some(READ_TIMEOUT))?;

    // No packets initially.
    assert!(&socket_rx.recv_from(&mut buf).is_err());

    let mut payload_size = 100;
    // Clone the socket as the async task takes ownership.
    send_udp_packets(socket_tx.try_clone()?, addr_rx, payload_size, 0).await?;
    assert!(&socket_rx.recv_from(&mut buf).is_err());

    send_udp_packets(socket_tx.try_clone()?, addr_rx, payload_size, 1).await?;
    let (recv_size, addr) = socket_rx.recv_from(&mut buf)?;
    assert_eq!(recv_size, payload_size);
    assert_eq!(addr, addr_tx);

    payload_size = 200;
    send_udp_packets(socket_tx.try_clone()?, addr_rx, payload_size, 3).await?;
    let (recv_size, _) = socket_rx.recv_from(&mut buf)?;
    assert_eq!(recv_size, payload_size);
    let (recv_size, _) = socket_rx.recv_from(&mut buf)?;
    assert_eq!(recv_size, payload_size);
    let (recv_size, _) = socket_rx.recv_from(&mut buf)?;
    assert_eq!(recv_size, payload_size);

    // TODO(NET-2558): Add a test for the empty payload case.
    Ok(())
}
