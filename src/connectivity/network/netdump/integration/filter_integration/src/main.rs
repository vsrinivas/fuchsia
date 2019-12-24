// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fuchsia_async as fasync,
    helper::*,
    net_types::ip::IpVersion,
    std::net::{SocketAddr, UdpSocket},
};

const PACKET_COUNT: usize = 10;
const PAYLOAD_SIZE: usize = 100; // octets.
const BIG_PAYLOAD_SIZE: usize = 1000; // octets.

fn main() -> Result<(), Error> {
    // `filter_integration` is too long for endpoint_names.
    const TEST_NAME: &str = "filter";
    let mut env = TestEnvironment::new(fasync::Executor::new()?, TEST_NAME)?;

    test_case!(timeout_test, &mut env);
    test_case!(bad_filters_test, &mut env);
    test_case!(positive_filter_test, &mut env);
    test_case!(negative_filter_test, &mut env);
    test_case!(long_filter_string_test, &mut env);

    Ok(())
}

// A test that specifying a timeout causes netdump to quit by itself.
// The timeout is only intended to be best-effort. Capture exiting successfully
// is the only criterion for the test to pass.
fn timeout_test(env: &mut TestEnvironment) -> Result<(), Error> {
    let mut args = env.new_args(EndpointType::RX).insert_timeout(0);
    let mut output = env.run_test_case_no_packets(args.into())?;
    assert!(output.ok().is_ok());

    args = env.new_args(EndpointType::RX).insert_timeout(1);
    output = env.run_test_case_no_packets(args.into())?;
    assert!(output.ok().is_ok());
    Ok(())
}

// A test of bad filter strings where packet capture exits immediately.
// Not intending to test every syntax error as that should be unit tested.
fn bad_filters_test(env: &mut TestEnvironment) -> Result<(), Error> {
    // Empty filter string.
    let mut args = env.new_args(EndpointType::RX).insert_filter("");
    let mut output = env.run_test_case_no_packets(args.into())?;
    assert!(output.ok().is_err());

    // Invalid keyword.
    args = env.new_args(EndpointType::RX).insert_filter("this is magic");
    output = env.run_test_case_no_packets(args.into())?;
    assert!(output.ok().is_err());

    // Invalid parenthesis.
    args = env.new_args(EndpointType::RX).insert_filter("udp and ( ip4 tcp port 80");
    output = env.run_test_case_no_packets(args.into())?;
    assert!(output.ok().is_err());

    // Invalid port name.
    args = env.new_args(EndpointType::RX).insert_filter("udp and ip4 tcp port htp");
    output = env.run_test_case_no_packets(args.into())?;
    assert!(output.ok().is_err());

    Ok(())
}

// Test the effect of a positive filter (no leading "not") on packet capture.
// Should expect only the packets accepted by the filter are captured.
fn positive_filter_test(env: &mut TestEnvironment) -> Result<(), Error> {
    let args = env
        .new_args(EndpointType::RX)
        .insert_packet_count(PACKET_COUNT)
        .insert_write_to_dumpfile(DEFAULT_DUMPFILE)
        .insert_filter(&format!("greater {}", BIG_PAYLOAD_SIZE));

    let socket_tx = UdpSocket::bind(EndpointType::TX.default_socket_addr(IpVersion::V4))?;

    let send_packets_fut = async move {
        // Interleave sending of big and small packets.
        for _ in 0..PACKET_COUNT {
            send_udp_packets(
                socket_tx.try_clone()?,
                EndpointType::RX.default_socket_addr(IpVersion::V4),
                PAYLOAD_SIZE,
                1,
            )
            .await?;
            send_udp_packets(
                socket_tx.try_clone()?,
                EndpointType::RX.default_socket_addr(IpVersion::V4),
                BIG_PAYLOAD_SIZE,
                1,
            )
            .await?;
        }
        Ok(())
    };
    let output = env.run_test_case(args.into(), send_packets_fut, DEFAULT_DUMPFILE)?;
    output.ok()?;

    let output_stdout = output_string(&output.stdout);
    // Obtained headers.
    let packets: Vec<String> = output_stdout.lines().map(String::from).collect();
    assert_eq!(packets.len(), PACKET_COUNT);

    // Expected data.
    let exp_length = format!("{}", 20 + 8 + BIG_PAYLOAD_SIZE); // IPv4 + UDP + Payload.

    for packet in packets {
        if !packet.contains(&exp_length) {
            return Err(format_err!("Unfiltered packet found! {}", packet));
        }
    }

    Ok(())
}

// Test the effect of a negative filter in packet capture.
// Should expect only the packets accepted by the filter are captured.
fn negative_filter_test(env: &mut TestEnvironment) -> Result<(), Error> {
    const ALT_PORT: u16 = 3333;

    let args = env
        .new_args(EndpointType::RX)
        .insert_packet_count(PACKET_COUNT)
        .insert_write_to_dumpfile(DEFAULT_DUMPFILE)
        .insert_filter(&format!("not port {}", EndpointType::TX.default_port()));

    let socket_tx = UdpSocket::bind(EndpointType::TX.default_socket_addr(IpVersion::V4))?;
    let alt_socket_addr =
        SocketAddr::new(EndpointType::TX.default_ip_addr(IpVersion::V4), ALT_PORT);
    let alt_socket_tx = UdpSocket::bind(alt_socket_addr)?;

    let send_packets_fut = async move {
        // Interleave sending from different ports.
        for _ in 0..PACKET_COUNT {
            send_udp_packets(
                socket_tx.try_clone()?,
                EndpointType::RX.default_socket_addr(IpVersion::V4),
                PAYLOAD_SIZE,
                1,
            )
            .await?;
            send_udp_packets(
                alt_socket_tx.try_clone()?,
                EndpointType::RX.default_socket_addr(IpVersion::V4),
                PAYLOAD_SIZE,
                1,
            )
            .await?;
        }
        Ok(())
    };
    let output = env.run_test_case(args.into(), send_packets_fut, DEFAULT_DUMPFILE)?;
    output.ok()?;

    let output_stdout = output_string(&output.stdout);
    // Obtained headers.
    let packets: Vec<String> = output_stdout.lines().map(String::from).collect();
    assert_eq!(packets.len(), PACKET_COUNT);

    for packet in packets {
        if packet.contains(&EndpointType::TX.default_port().to_string()) {
            return Err(format_err!("Unfiltered packet found! {}", packet));
        }
    }

    Ok(())
}

// Test that a long filter string works.
// Only testing a subcomponent of the filter.
fn long_filter_string_test(env: &mut TestEnvironment) -> Result<(), Error> {
    const ALT_PORT: u16 = 2345;
    const FILTER_STR: &str = "not ( port 65026,65268,ssh or dst port 8083 or ip6 dst port \
                              33330-33341 or proto udp dst port 2345 or ip4 udp dst port 1900 )";

    let args = env
        .new_args(EndpointType::RX)
        .insert_packet_count(PACKET_COUNT)
        .insert_write_to_dumpfile(DEFAULT_DUMPFILE)
        .insert_filter(FILTER_STR);

    let socket_tx = UdpSocket::bind(EndpointType::TX.default_socket_addr(IpVersion::V4))?;
    let alt_socket_addr =
        SocketAddr::new(EndpointType::RX.default_ip_addr(IpVersion::V4), ALT_PORT);

    let send_packets_fut = async move {
        // Interleave sending from different ports.
        for _ in 0..PACKET_COUNT {
            send_udp_packets(
                socket_tx.try_clone()?,
                EndpointType::RX.default_socket_addr(IpVersion::V4),
                PAYLOAD_SIZE,
                1,
            )
            .await?;
            send_udp_packets(socket_tx.try_clone()?, alt_socket_addr, PAYLOAD_SIZE, 1).await?;
        }
        Ok(())
    };
    let output = env.run_test_case(args.into(), send_packets_fut, DEFAULT_DUMPFILE)?;
    output.ok()?;

    let output_stdout = output_string(&output.stdout);
    // Obtained headers.
    let packets: Vec<String> = output_stdout.lines().map(String::from).collect();
    assert_eq!(packets.len(), PACKET_COUNT);

    for packet in packets {
        if packet.contains("2345") {
            return Err(format_err!("Unfiltered packet found! {}", packet));
        }
    }

    Ok(())
}
