// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fdio::{SpawnAction, SpawnOptions};
use fidl_fuchsia_posix_socket as fposix_socket;
use fidl_fuchsia_posix_socket_packet as fposix_socket_packet;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_runtime::{duplicate_utc_clock_handle, job_default, HandleInfo, HandleType};
use fuchsia_zircon::{self as zx, HandleBased as _, ProcessInfo};
use futures::{
    future,
    io::{AsyncBufReadExt, AsyncReadExt, BufReader},
    stream::StreamExt as _,
};
use libc::{STDERR_FILENO, STDOUT_FILENO};
use net_declare::std_socket_addr;
use netemul::RealmUdpSocket as _;
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use regex::Regex;
use std::{
    convert::TryInto as _,
    ffi::{CStr, CString},
};

const BINARY_PATH: &str = "/pkg/bin/tcpdump";

/// Returns true iff the patterns were found, false if the stream ended.
async fn wait_for_pattern<SO: AsyncBufReadExt + Unpin, SE: AsyncReadExt + Unpin>(
    reader: &mut SO,
    other_reader: &mut SE,
    mut patterns: Vec<regex::Regex>,
) {
    while !patterns.is_empty() {
        let mut line = String::new();
        let read_bytes = reader.read_line(&mut line).await.expect("read_line");

        if read_bytes == 0 {
            let mut buf = String::new();
            let _read_bytes: usize =
                other_reader.read_to_string(&mut buf).await.expect("read other reader");
            panic!(
                "failed to match all patterns from reader; patterns = {:?}\nOTHER READER:\n{}",
                patterns, buf
            )
        }
        // Trim the trailing new line.
        let line = &line[..line.len() - 1];
        let () = patterns.retain(|pattern| !pattern.is_match(&line));
    }
}

fn start_tcpdump(
    args: impl IntoIterator<Item = &'static str>,
    mut spawn_actions: Vec<SpawnAction<'_>>,
) -> (zx::Process, zx::Socket, zx::Socket) {
    let (stdout_reader, stdout_writer) =
        zx::Socket::create(zx::SocketOpts::STREAM).expect("create stdout socket");
    let (stderr_reader, stderr_writer) =
        zx::Socket::create(zx::SocketOpts::STREAM).expect("create stderr socket");

    // The reader-ends should not write.
    let () = stdout_reader.half_close().expect("stdout_reader.half_close");
    let () = stderr_reader.half_close().expect("stderr_reader.half_close");

    let path = CString::new(BINARY_PATH).expect("cstring path");
    let path = path.as_c_str();

    let args: Vec<CString> = std::iter::once(BINARY_PATH)
        .chain(args.into_iter())
        .map(|a| {
            CString::new(a).unwrap_or_else(|e| panic!("failed to parse {} to CString: {}", a, e))
        })
        .collect();
    let args: Vec<&CStr> = args.iter().map(|s| s.as_c_str()).collect();

    // Provide the socket that TCPDump should use for stdout.
    spawn_actions.push(SpawnAction::add_handle(
        HandleInfo::new(
            HandleType::FileDescriptor,
            STDOUT_FILENO.try_into().expect("STDOUT_FILENO.try_into"),
        ),
        stdout_writer.into(),
    ));
    // Provide the socket that TCPDump should use for stderr.
    spawn_actions.push(SpawnAction::add_handle(
        HandleInfo::new(
            HandleType::FileDescriptor,
            STDERR_FILENO.try_into().expect("STDERR_FILENO.try_into"),
        ),
        stderr_writer.into(),
    ));

    let process = fdio::spawn_etc(
        &job_default(),
        SpawnOptions::DEFAULT_LOADER,
        path,
        &args[..],
        None,
        &mut spawn_actions,
    )
    .expect("spawn tcpdump");

    (process, stdout_reader, stderr_reader)
}

#[fuchsia::test]
async fn version_test() {
    let (process, stdout_reader, stderr_reader) = start_tcpdump(["--version"], Vec::new());

    assert_eq!(
        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .expect("wait for process termination"),
        zx::Signals::PROCESS_TERMINATED
    );
    let ProcessInfo { return_code, start_time: _, flags: _ } =
        process.info().expect("process info");
    assert_eq!(return_code, 0);

    let mut stdout_reader =
        BufReader::new(fasync::Socket::from_socket(stdout_reader).expect("async socket stdout"));
    let mut stderr_reader =
        fasync::Socket::from_socket(stderr_reader).expect("async socket stderr");

    wait_for_pattern(
        &mut stdout_reader,
        &mut stderr_reader,
        vec![
            Regex::new(r"tcpdump version 4\.99\.0").expect("parse tcpdump version regex"),
            Regex::new(r"libpcap version 1\.10\.1").expect("parse libpcap version regex"),
        ],
    )
    .await
}

#[fuchsia::test]
// TODO(https://fxbug.dev/88133): Fix memory leak and run this with Lsan.
#[cfg_attr(feature = "variant_asan", ignore)]
async fn packet_test() {
    let name = "packet_test";
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");

    let (svc_client_end, svc_server_end) = zx::Channel::create().expect("create channel");
    let (process, stdout_reader, stderr_reader) = start_tcpdump(
        ["-c", "1", "--no-promiscuous-mode"],
        vec![
            SpawnAction::add_namespace_entry(
                CString::new("/svc").expect("CString /svc").as_c_str(),
                svc_client_end.into_handle(),
            ),
            SpawnAction::add_handle(
                HandleInfo::new(HandleType::ClockUtc, 0),
                duplicate_utc_clock_handle(
                    zx::Rights::READ | zx::Rights::WAIT | zx::Rights::TRANSFER,
                )
                .expect("duplicate utc clock handle")
                .into_handle(),
            ),
        ],
    );
    let mut stdout_reader =
        BufReader::new(fasync::Socket::from_socket(stdout_reader).expect("async socket stdout"));
    let mut stderr_reader =
        BufReader::new(fasync::Socket::from_socket(stderr_reader).expect("async socket stdout"));

    let realm = &realm;

    let mut svcfs = ServiceFs::new_local();
    let svcfs = svcfs
        .add_service_connector::<_, fposix_socket::ProviderMarker>(|server_end| {
            realm
                .connect_to_protocol_with_server_end(server_end)
                .expect("connect to regular socket provider")
        })
        .add_service_connector::<_, fposix_socket_packet::ProviderMarker>(|server_end| {
            realm
                .connect_to_protocol_with_server_end(server_end)
                .expect("connect to packet socket provider")
        })
        .serve_connection(svc_server_end)
        .expect("servicefs serve connection")
        .collect::<()>();

    // Wait for TCPDump to start.
    let svcfs = {
        let wait_for_pattern_fut = wait_for_pattern(
            &mut stderr_reader,
            &mut stdout_reader,
            vec![regex::Regex::new(r"listening on any, link-type LINUX_SLL2 \(Linux cooked v2\), snapshot length \d+ bytes").expect("parse tcpdump listening regex")],
        );
        futures::pin_mut!(wait_for_pattern_fut);
        match future::select(wait_for_pattern_fut, svcfs).await {
            future::Either::Left(((), svcfs)) => svcfs,
            future::Either::Right(((), _wait_for_pattern_fut)) => {
                panic!("service directory unexpectedly ended")
            }
        }
    };

    // Send a UDP packet and make sure TCPDump logs it.
    let sock =
        fuchsia_async::net::UdpSocket::bind_in_realm(&realm, std_socket_addr!("127.0.0.1:0"))
            .await
            .expect("create socket");
    let addr = sock.local_addr().expect("get bound socket address");
    const PAYLOAD: [u8; 4] = [1, 2, 3, 4];
    let sent = sock.send_to(&PAYLOAD[..], addr).await.expect("send_to failed");
    assert_eq!(sent, PAYLOAD.len());

    // TCPDump should terminate after reading a single packet as it was started
    // with the "-c 1" args.
    assert_eq!(
        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .expect("wait for process termination"),
        zx::Signals::PROCESS_TERMINATED
    );
    let ProcessInfo { return_code, start_time: _, flags: _ } =
        process.info().expect("process info");
    assert_eq!(return_code, 0);

    {
        let wait_for_pattern_fut = wait_for_pattern(
            &mut stdout_reader,
            &mut stderr_reader,
            vec![regex::Regex::new(
                r"lo\s+In\s+IP 127\.0\.0\.1\.\d+ > 127\.0\.0\.1\.\d+: UDP, length 4",
            )
            .expect("parse tcpdump packet regex")],
        );
        futures::pin_mut!(wait_for_pattern_fut);
        match future::select(wait_for_pattern_fut, svcfs).await {
            future::Either::Left(((), _svcfs)) => {}
            future::Either::Right(((), _wait_for_pattern_fut)) => {
                panic!("service directory unexpectedly ended")
            }
        }
    }
}
