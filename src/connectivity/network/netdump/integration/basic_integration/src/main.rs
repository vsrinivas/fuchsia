// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fuchsia_async as fasync,
    helper::*,
    net_types::ip::IpVersion,
    std::net::UdpSocket,
};

const PACKET_COUNT: usize = 10;
const PAYLOAD_SIZE: usize = 100; // octets.

fn main() -> Result<(), Error> {
    static TEST_NAME: &str = "basic_integration";
    let mut env = TestEnvironment::new(fasync::Executor::new()?, TEST_NAME)?;

    // Example integration tests of basic packet sniffing functionality.
    // TODO(CONN-168): Add more comprehensive tests.
    test_case!(bad_args_test, &mut env);

    test_case!(receive_one_of_many_test, &mut env, EndpointType::RX);
    // Repeat for capturing on TX endpoint.
    test_case!(receive_one_of_many_test, &mut env, EndpointType::TX);

    Ok(())
}

// A simple test of bad arguments where packet capture exits immediately.
fn bad_args_test(env: &mut TestEnvironment) -> Result<(), Error> {
    // Empty arguments.
    let mut output = env.run_test_case_no_packets(vec![])?;
    assert!(output.ok().is_err());

    // Bad device.
    output = env.run_test_case_no_packets(vec!["..".into()])?;
    assert!(output.ok().is_err());

    // Device should be last.
    let mut argsv: Vec<String> = env.new_args(EndpointType::RX).into();
    argsv.push("-v".into());
    output = env.run_test_case_no_packets(argsv)?;
    assert!(output.ok().is_err());

    // Unknown argument.
    let args = env.new_args(EndpointType::RX).insert_arg("-x".into());
    output = env.run_test_case_no_packets(args.into())?;
    assert!(output.ok().is_err());

    Ok(())
}

// Tests that packet headers are captured and output.
// Only one correct packet is necessary for the test to pass
fn receive_one_of_many_test(
    env: &mut TestEnvironment,
    capture_ep: EndpointType,
) -> Result<(), Error> {
    let args = env
        .new_args(capture_ep)
        .insert_packet_count(PACKET_COUNT)
        .insert_write_to_dumpfile(DEFAULT_DUMPFILE);

    let socket_tx = UdpSocket::bind(EndpointType::TX.default_socket_addr(IpVersion::V4))?;
    let output = env.run_test_case(
        args.into(),
        send_udp_packets(
            socket_tx,
            EndpointType::RX.default_socket_addr(IpVersion::V4),
            PAYLOAD_SIZE,
            PACKET_COUNT,
        ),
        DEFAULT_DUMPFILE,
    )?;
    output.ok()?;

    let output_stdout = output_string(&output.stdout);
    // Obtained headers.
    let packets: Vec<String> = output_stdout.lines().map(String::from).collect();
    assert_eq!(packets.len(), PACKET_COUNT);

    // Expected data.
    let headers = [
        "IP4",
        EndpointType::TX.default_ip_addr_str(IpVersion::V4),
        EndpointType::RX.default_ip_addr_str(IpVersion::V4),
        &EndpointType::TX.default_port().to_string(),
        &EndpointType::RX.default_port().to_string(),
        "UDP",
        &(20 + 8 + PAYLOAD_SIZE).to_string(), // IP + UDP headers + payload octets in length.
    ];

    for packet in packets {
        match check_all_substrs(&packet, &headers) {
            Ok(()) => return Ok(()),
            _ => continue,
        }
    }

    Err(format_err!("Matching packet not found in packet_data_test!"))
}
