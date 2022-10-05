// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{arguments, services},
    anyhow::{anyhow, Error},
    fidl::endpoints::{create_proxy, create_request_stream},
    fidl_fuchsia_virtualization::{
        GuestMarker, GuestStatus, HostVsockAcceptorMarker, HostVsockEndpointMarker,
    },
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, HandleBased},
    futures::{select, try_join, AsyncReadExt, AsyncWriteExt, FutureExt, TryStreamExt},
    std::{
        collections::{HashMap, HashSet},
        io::Write,
    },
};

const HOST_PORT: u32 = 8500;
const CONTROL_STREAM: u32 = 8501;
const LATENCY_CHECK_STREAM: u32 = 8502;

const SINGLE_STREAM_THROUGHPUT: u32 = 8503;
const SINGLE_STREAM_MAGIC_NUM: u8 = 123;

const MULTI_STREAM_THROUGHPUT1: u32 = 8504;
const MULTI_STREAM_MAGIC_NUM1: u8 = 124;
const MULTI_STREAM_THROUGHPUT2: u32 = 8505;
const MULTI_STREAM_MAGIC_NUM2: u8 = 125;
const MULTI_STREAM_THROUGHPUT3: u32 = 8506;
const MULTI_STREAM_MAGIC_NUM3: u8 = 126;
const MULTI_STREAM_THROUGHPUT4: u32 = 8507;
const MULTI_STREAM_MAGIC_NUM4: u8 = 127;
const MULTI_STREAM_THROUGHPUT5: u32 = 8508;
const MULTI_STREAM_MAGIC_NUM5: u8 = 128;

const SINGLE_STREAM_BIDIRECTIONAL: u32 = 8509;
const SINGLE_STREAM_BIDIRECTIONAL_MAGIC_NUM: u8 = 129;

fn percentile(durations: &[u64], percentile: u8) -> u64 {
    assert!(percentile <= 100 && !durations.is_empty());
    // Don't bother interpolating between two points if this isn't a whole number, just floor it.
    let location = (((percentile as f64) / 100.0) * ((durations.len() - 1) as f64)) as usize;
    durations[location]
}

fn latency_percentile_string(durations: &[u64]) -> String {
    format!(
        "\tMin: {}ns ({:.3}ms)\
             \n\t25th percentile: {}ns ({:.3}ms)\
             \n\t50th percentile: {}ns ({:.3}ms)\
             \n\t75th percentile: {}ns ({:.3}ms)\
             \n\t99th percentile: {}ns ({:.3}ms)\
             \n\tMax: {}ns ({:.3}ms)",
        percentile(&durations, 0),
        (percentile(&durations, 0) as f64) / 1000000.0,
        percentile(&durations, 25),
        (percentile(&durations, 25) as f64) / 1000000.0,
        percentile(&durations, 50),
        (percentile(&durations, 50) as f64) / 1000000.0,
        percentile(&durations, 75),
        (percentile(&durations, 75) as f64) / 1000000.0,
        percentile(&durations, 99),
        (percentile(&durations, 99) as f64) / 1000000.0,
        percentile(&durations, 100),
        (percentile(&durations, 100) as f64) / 1000000.0
    )
}

fn throughput_percentile_string(durations: &[u64], bytes: usize) -> String {
    let to_mebibytes_per_second = |nanos: u64| -> f64 {
        let seconds = nanos as f64 / (1000.0 * 1000.0 * 1000.0);
        let bytes_per_second = (bytes as f64) / seconds;
        bytes_per_second / (1 << 20) as f64
    };

    format!(
        "\tMin: {:.2}MiB/s\
        \n\t25th percentile: {:.2}MiB/s\
        \n\t50th percentile: {:.2}MiB/s\
        \n\t75th percentile: {:.2}MiB/s\
        \n\t99thth percentile: {:.2}MiB/s\
        \n\tMax: {:.2}MiB/s",
        to_mebibytes_per_second(percentile(&durations, 0)),
        to_mebibytes_per_second(percentile(&durations, 25)),
        to_mebibytes_per_second(percentile(&durations, 50)),
        to_mebibytes_per_second(percentile(&durations, 75)),
        to_mebibytes_per_second(percentile(&durations, 99)),
        to_mebibytes_per_second(percentile(&durations, 100)),
    )
}

async fn warmup_and_data_corruption_check(
    socket: &mut fasync::Socket,
    result: &mut String,
) -> Result<(), Error> {
    // Send and receive 100 messages, checking for a known but changing pattern.
    let mut buffer = vec![0u8; 4096];
    for i in 0..100 {
        let pattern = format!("DAVID{:0>3}", i).repeat(512);
        let packet = pattern.as_bytes();
        assert_eq!(packet.len(), buffer.len());

        if packet.len() != socket.as_ref().write(&packet)? {
            return Err(anyhow!("failed to write full packet"));
        }

        let timeout = fasync::Time::after(zx::Duration::from_millis(100));
        select! {
            () = fasync::Timer::new(timeout).fuse() => {
                return Err(anyhow!("warmup timed out waiting 100ms for a packet echoed"));
            }
            result = socket.read_exact(&mut buffer).fuse() => {
                result.map_err(|err| anyhow!("failed to read from socket during warmup: {}", err))?;
            }
        }

        if buffer != packet {
            *result += "\nData corruption check: FAILED";
            return Ok(());
        }
    }

    *result += "\n* Data corruption check: PASSED";

    Ok(())
}

// Get the magic numbers for a test case from the guest to know that it's ready.
async fn wait_for_magic_numbers(
    mut numbers: HashSet<u8>,
    control_socket: &mut fasync::Socket,
) -> Result<(), Error> {
    let timeout = fasync::Time::after(zx::Duration::from_seconds(5));
    let mut magic_buf = [0u8];
    loop {
        select! {
            () = fasync::Timer::new(timeout).fuse() => {
                return Err(anyhow!("timeout waiting 5s to get the test ready"));
            }
            result = control_socket.read_exact(&mut magic_buf).fuse() => {
                result.map_err(|err| anyhow!("failed to read magic value from socket: {}", err))?;
                match numbers.contains(&magic_buf[0]) {
                    false => Err(anyhow!("unexpected magic number from guest: {}", magic_buf[0])),
                    true => {
                        numbers.remove(&magic_buf[0]);
                        Ok(())
                    }
                }?;

                if numbers.is_empty() {
                    break;
                }
            }
        }
    }

    Ok(())
}

async fn read_single_stream(
    total_size: usize,
    socket: &mut fasync::Socket,
) -> Result<fasync::Time, Error> {
    let timeout = fasync::Time::after(zx::Duration::from_seconds(10));
    let mut buffer = [0u8; 4096]; // 4 KiB
    let segments = total_size / buffer.len();

    for _ in 0..segments {
        select! {
            () = fasync::Timer::new(timeout).fuse() => {
                return Err(anyhow!("timeout waiting 10s for test iteration read to finish"));
            }
            result = socket.read_exact(&mut buffer).fuse() => {
                result.map_err(|err| anyhow!("failed to read segment from socket: {}", err))?;
            }
        }
    }

    Ok(fasync::Time::now())
}

async fn write_single_stream(
    total_size: usize,
    socket: &mut fasync::Socket,
) -> Result<fasync::Time, Error> {
    let timeout = fasync::Time::after(zx::Duration::from_seconds(10));
    let buffer = [0u8; 4096]; // 4 KiB
    let segments = total_size / buffer.len();

    for _ in 0..segments {
        select! {
            () = fasync::Timer::new(timeout).fuse() => {
                return Err(anyhow!("timeout waiting 10s for test iteration write to finish"));
            }
            result = socket.write_all(&buffer).fuse() => {
                result.map_err(
                    |err| anyhow!("failed to write segment to socket: {}", err))?;
            }
        }
    }

    Ok(fasync::Time::now())
}

async fn write_read_high_throughput(
    total_size: usize,
    socket: &mut fasync::Socket,
) -> Result<(), Error> {
    // This is intentionally sequential to measure roundtrip throughput from the perspective of
    // the host.
    write_single_stream(total_size, socket).await?;
    read_single_stream(total_size, socket).await?;
    Ok(())
}

async fn run_single_stream_bidirectional_test(
    mut read_socket: fasync::Socket,
    control_socket: &mut fasync::Socket,
    result: &mut String,
) -> Result<(), Error> {
    println!("Starting single stream bidirectional round trip throughput test...");

    let mut write_socket = fasync::Socket::from_socket(
        read_socket.as_ref().duplicate_handle(zx::Rights::SAME_RIGHTS)?,
    )?;

    wait_for_magic_numbers(HashSet::from([SINGLE_STREAM_BIDIRECTIONAL_MAGIC_NUM]), control_socket)
        .await?;

    let total_size = (1 << 20) * 128; // 128 MiB
    let mut rx_durations: Vec<u64> = Vec::new();
    let mut tx_durations: Vec<u64> = Vec::new();

    for i in 0..100 {
        let before = fasync::Time::now();

        let (write_finish, read_finish) = try_join!(
            write_single_stream(total_size, &mut write_socket),
            read_single_stream(total_size, &mut read_socket)
        )?;

        rx_durations.push(
            (write_finish.into_nanos() - before.into_nanos())
                .try_into()
                .expect("durations measured by the same thread must be greater than zero"),
        );

        tx_durations.push(
            (read_finish.into_nanos() - before.into_nanos())
                .try_into()
                .expect("durations measured by the same thread must be greater than zero"),
        );

        print!("\rFinished {} bidirectional throughput measurements", i + 1);
        std::io::stdout().flush().expect("failed to flush stdout");
    }

    rx_durations.sort();
    rx_durations.reverse();

    tx_durations.sort();
    tx_durations.reverse();

    assert_eq!(rx_durations.len(), tx_durations.len());
    println!("\rFinished {} bidirectional throughput measurements", rx_durations.len());

    *result += format!(
        "\n* TX (guest -> host, unreliable) throughput of {} MiB:\n{}",
        total_size / (1 << 20),
        throughput_percentile_string(&tx_durations, total_size)
    )
    .as_str();

    *result += format!(
        "\n* RX (host -> guest, unreliable) throughput of {} MiB:\n{}",
        total_size / (1 << 20),
        throughput_percentile_string(&rx_durations, total_size)
    )
    .as_str();

    Ok(())
}

async fn run_single_stream_unidirectional_round_trip_test(
    mut data_socket: fasync::Socket,
    control_socket: &mut fasync::Socket,
    result: &mut String,
) -> Result<(), Error> {
    println!("Starting single stream unidirectional round trip throughput test...");

    wait_for_magic_numbers(HashSet::from([SINGLE_STREAM_MAGIC_NUM]), control_socket).await?;

    let total_size = (1 << 20) * 128; // 128 MiB
    let mut durations: Vec<u64> = Vec::new();

    for i in 0..100 {
        let before = fasync::Time::now();

        write_read_high_throughput(total_size, &mut data_socket).await?;

        let after = fasync::Time::now();
        durations.push(
            (after.into_nanos() - before.into_nanos())
                .try_into()
                .expect("durations measured by the same thread must be greater than zero"),
        );

        print!("\rFinished {} round trip throughput measurements", i + 1);
        std::io::stdout().flush().expect("failed to flush stdout");
    }

    durations.sort();
    durations.reverse();
    println!("\rFinished {} single stream round trip throughput measurements", durations.len());

    *result += format!(
        "\n* Single stream unidirectional round trip throughput of {} MiB:\n{}",
        total_size / (1 << 20),
        throughput_percentile_string(&durations, total_size * 2)
    )
    .as_str();

    Ok(())
}

async fn run_multi_stream_unidirectional_round_trip_test(
    mut data_socket1: fasync::Socket,
    mut data_socket2: fasync::Socket,
    mut data_socket3: fasync::Socket,
    mut data_socket4: fasync::Socket,
    mut data_socket5: fasync::Socket,
    control_socket: &mut fasync::Socket,
    result: &mut String,
) -> Result<(), Error> {
    println!("Starting multistream unidirectional round trip throughput test...");

    wait_for_magic_numbers(
        HashSet::from([
            MULTI_STREAM_MAGIC_NUM1,
            MULTI_STREAM_MAGIC_NUM2,
            MULTI_STREAM_MAGIC_NUM3,
            MULTI_STREAM_MAGIC_NUM4,
            MULTI_STREAM_MAGIC_NUM5,
        ]),
        control_socket,
    )
    .await?;

    let total_size = (1 << 20) * 128; // 128 MiB
    let mut durations: Vec<u64> = Vec::new();

    for i in 0..50 {
        let before = fasync::Time::now();

        try_join!(
            write_read_high_throughput(total_size, &mut data_socket1),
            write_read_high_throughput(total_size, &mut data_socket2),
            write_read_high_throughput(total_size, &mut data_socket3),
            write_read_high_throughput(total_size, &mut data_socket4),
            write_read_high_throughput(total_size, &mut data_socket5)
        )?;

        let after = fasync::Time::now();
        durations.push(
            (after.into_nanos() - before.into_nanos())
                .try_into()
                .expect("durations measured by the same thread must be greater than zero"),
        );

        print!("\rFinished {} multistream round trip throughput measurements", i + 1);
        std::io::stdout().flush().expect("failed to flush stdout");
    }

    durations.sort();
    durations.reverse();
    println!("\rFinished {} multistream round trip throughput measurements", durations.len());

    *result += format!(
        "\n* Multistream (5 connections) unidirectional round trip throughput of {} MiB:\n{}",
        total_size / (1 << 20),
        throughput_percentile_string(&durations, total_size * 2)
    )
    .as_str();

    Ok(())
}

async fn run_latency_test(mut socket: fasync::Socket, result: &mut String) -> Result<(), Error> {
    println!("Checking for data corruption...");
    warmup_and_data_corruption_check(&mut socket, result).await?;
    println!("Finished data corruption check");

    let packet = [42u8; 4096];
    let mut buffer = vec![0u8; packet.len()];
    let mut latencies: Vec<u64> = Vec::new();

    println!("Starting latency test...");
    for i in 0..10000 {
        let timeout = fasync::Time::after(zx::Duration::from_millis(100));
        let before = fasync::Time::now();

        if packet.len() != socket.as_ref().write(&packet)? {
            return Err(anyhow!("failed to write full packet"));
        }

        select! {
            () = fasync::Timer::new(timeout).fuse() => {
                return Err(anyhow!("latency test timed out waiting 100ms for a packet echoed"));
            }
            result = socket.read_exact(&mut buffer).fuse() => {
                result.map_err(
                    |err| anyhow!("failed to read from socket during latency test: {}", err))?;
            }
        }

        let after = fasync::Time::now();
        latencies.push(
            (after.into_nanos() - before.into_nanos())
                .try_into()
                .expect("durations measured by the same thread must be greater than zero"),
        );

        if (i + 1) % 50 == 0 {
            print!("\rFinished measuring round trip latency for {} packets", i + 1);
            std::io::stdout().flush().expect("failed to flush stdout");
        }
    }

    latencies.sort();
    println!("\rFinished measuring round trip latency for {} packets", latencies.len());

    *result += format!(
        "\n* Round trip latency of {} bytes:\n{}",
        packet.len(),
        latency_percentile_string(&latencies)
    )
    .as_str();

    Ok(())
}

pub async fn run_micro_benchmark(guest_type: arguments::GuestType) -> Result<(), Error> {
    let guest_manager = services::connect_to_manager(guest_type)?;
    let guest_info = guest_manager.get_info().await?;
    if guest_info.guest_status.unwrap() != GuestStatus::Running {
        return Err(anyhow!(zx::Status::NOT_CONNECTED));
    }

    let (guest_endpoint, guest_server_end) = create_proxy::<GuestMarker>()
        .map_err(|err| anyhow!("failed to create guest proxy: {}", err))?;
    guest_manager
        .connect(guest_server_end)
        .await
        .map_err(|err| anyhow!("failed to get a connect response: {}", err))?
        .map_err(|err| anyhow!("connect failed with: {:?}", err))?;

    let (vsock_endpoint, vsock_server_end) = create_proxy::<HostVsockEndpointMarker>()
        .map_err(|err| anyhow!("failed to create vsock proxy: {}", err))?;
    guest_endpoint
        .get_host_vsock_endpoint(vsock_server_end)
        .await?
        .map_err(|err| anyhow!("failed to get HostVsockEndpoint: {:?}", err))?;

    let (acceptor, mut client_stream) = create_request_stream::<HostVsockAcceptorMarker>()
        .map_err(|err| anyhow!("failed to create vsock acceptor: {}", err))?;
    vsock_endpoint
        .listen(HOST_PORT, acceptor)
        .await
        .map_err(|err| anyhow!("failed to get a listen response: {}", err))?
        .map_err(|err| anyhow!("listen failed with: {}", zx::Status::from_raw(err)))?;

    let socket = guest_endpoint
        .get_console()
        .await
        .map_err(|err| anyhow!("failed to get a get_console response: {}", err))?
        .map_err(|err| anyhow!("get_console failed with: {:?}", err))?;

    // Start the micro benchmark utility on the guest which will begin by opening the necessary
    // connections.
    let command = b"../test_utils/virtio_vsock_test_util micro_benchmark\n";
    let bytes_written = socket
        .write(command)
        .map_err(|err| anyhow!("failed to write command to socket: {}", err))?;
    if bytes_written != command.len() {
        return Err(anyhow!(
            "attempted to send command '{}', but only managed to write '{}'",
            std::str::from_utf8(command).expect("failed to parse as utf-8"),
            std::str::from_utf8(&command[0..bytes_written]).expect("failed to parse as utf-8")
        ));
    }

    let mut expected_connections = HashSet::from([
        CONTROL_STREAM,
        LATENCY_CHECK_STREAM,
        SINGLE_STREAM_THROUGHPUT,
        MULTI_STREAM_THROUGHPUT1,
        MULTI_STREAM_THROUGHPUT2,
        MULTI_STREAM_THROUGHPUT3,
        MULTI_STREAM_THROUGHPUT4,
        MULTI_STREAM_THROUGHPUT5,
        SINGLE_STREAM_BIDIRECTIONAL,
    ]);
    let mut active_connections = HashMap::new();

    // Give the utility 15s to open all the expected connections.
    let timeout = fasync::Time::after(zx::Duration::from_seconds(15));
    loop {
        select! {
            () = fasync::Timer::new(timeout).fuse() => {
                return Err(anyhow!("vsockperf timed out waiting 15s for vsock connections"));
            }
            request = client_stream.try_next() => {
                let request = request
                    .map_err(|err| anyhow!("failed to get acceptor request: {}", err))?
                    .ok_or(anyhow!("unexpected end of Listener stream"))?;
                let (_src_cid, src_port, _port, responder) = request
                    .into_accept().ok_or(anyhow!("failed to parse message as Accept"))?;

                match expected_connections.contains(&src_port) {
                    false => Err(anyhow!("unexpected connection from guest port: {}", src_port)),
                    true => {
                        expected_connections.remove(&src_port);
                        Ok(())
                    }
                }?;

                let (client_socket, device_socket) = zx::Socket::create(zx::SocketOpts::STREAM)
                    .map_err(|err| anyhow!("failed to create sockets: {}", err))?;
                let client_socket = fasync::Socket::from_socket(client_socket)?;

                responder.send(&mut Ok(device_socket))
                    .map_err(|err| anyhow!("failed to send response to device: {}", err))?;

                if let Some(_) = active_connections.insert(src_port, client_socket) {
                    panic!("Connections must be unique");
                }

                if expected_connections.is_empty() {
                    break;
                }
            }
        }
    }

    let mut result_message: String =
        "\n\nMicro Benchmark Results\n------------------------".to_owned();

    run_latency_test(
        active_connections.remove(&LATENCY_CHECK_STREAM).expect("socket should exist"),
        &mut result_message,
    )
    .await?;

    run_single_stream_bidirectional_test(
        active_connections.remove(&SINGLE_STREAM_BIDIRECTIONAL).expect("socket should exist"),
        active_connections.get_mut(&CONTROL_STREAM).expect("socket should exist"),
        &mut result_message,
    )
    .await?;

    run_single_stream_unidirectional_round_trip_test(
        active_connections.remove(&SINGLE_STREAM_THROUGHPUT).expect("socket should exist"),
        active_connections.get_mut(&CONTROL_STREAM).expect("socket should exist"),
        &mut result_message,
    )
    .await?;

    run_multi_stream_unidirectional_round_trip_test(
        active_connections.remove(&MULTI_STREAM_THROUGHPUT1).expect("socket should exist"),
        active_connections.remove(&MULTI_STREAM_THROUGHPUT2).expect("socket should exist"),
        active_connections.remove(&MULTI_STREAM_THROUGHPUT3).expect("socket should exist"),
        active_connections.remove(&MULTI_STREAM_THROUGHPUT4).expect("socket should exist"),
        active_connections.remove(&MULTI_STREAM_THROUGHPUT5).expect("socket should exist"),
        active_connections.get_mut(&CONTROL_STREAM).expect("socket should exist"),
        &mut result_message,
    )
    .await?;

    println!("{}", result_message);

    return Ok(());
}
