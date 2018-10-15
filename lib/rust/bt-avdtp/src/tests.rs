// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use fuchsia_zircon::{self as zx, Status};
use futures::executor::block_on;
use futures::{future, StreamExt, TryStreamExt};
use pin_utils::pin_mut;

use crate::*;

fn setup_peer() -> (Peer, zx::Socket) {
    let (remote, signaling) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();

    let peer = Peer::new(signaling);
    assert!(peer.is_ok());
    (peer.unwrap(), remote)
}

#[test]
fn closes_socket_when_dropped() {
    let mut _exec = fasync::Executor::new().expect("failed to create an executor");
    let (peer_sock, signaling) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();

    {
        let peer = Peer::new(signaling);
        assert!(peer.is_ok());
        let mut _stream = peer.unwrap().take_request_stream();
    }

    // Writing to the sock from the other end should fail.
    let write_res = peer_sock.write(&[0; 1]);
    assert!(write_res.is_err());
    assert_eq!(Status::PEER_CLOSED, write_res.err().unwrap());
}

#[test]
#[should_panic] // TODO: can't use catch_unwind here because of PeerInner?
fn can_only_take_stream_once() {
    let mut _exec = fasync::Executor::new().expect("failed to create an executor");
    let (_, signaling) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();

    let p = Peer::new(signaling);
    assert!(p.is_ok());
    let peer = p.unwrap();
    let mut _stream = peer.take_request_stream();
    peer.take_request_stream();
}

// Generic Request tests

const CMD_DISCOVER: &'static [u8] = &[
    0x40, // TxLabel (4), Single Packet (0b00), Command (0b00)
    0x01, // RFA (0b00), Discover (0x01)
];

fn setup_stream_test() -> (RequestStream, Peer, zx::Socket, fasync::Executor) {
    let exec = fasync::Executor::new().expect("failed to create an executor");
    let (peer, remote) = setup_peer();
    let stream = peer.take_request_stream();
    (stream, peer, remote, exec)
}

fn next_request(stream: &mut RequestStream, exec: &mut fasync::Executor) -> Request {
    let mut fut = stream.next();
    let complete = exec.run_until_stalled(&mut fut);

    match complete {
        Poll::Ready(Some(Ok(r))) => r,
        _ => panic!("should have a request"),
    }
}

fn recv_remote(remote: &zx::Socket) -> Result<Vec<u8>, zx::Status> {
    let waiting = remote.outstanding_read_bytes();
    assert!(waiting.is_ok());
    let mut response: Vec<u8> = vec![0; waiting.unwrap()];
    let response_read = remote.read(response.as_mut_slice())?;
    assert_eq!(response.len(), response_read);
    Ok(response)
}

fn expect_remote_recv(expected: &[u8], remote: &zx::Socket) {
    let r = recv_remote(&remote);
    assert!(r.is_ok());
    let response = r.unwrap();
    assert_eq!(expected.len(), response.len());
    assert_eq!(expected, &response[0..expected.len()]);
}

#[test]
fn closed_peer_ends_request_stream() {
    let (stream, _, _, _) = setup_stream_test();
    let collected = block_on(stream.collect::<Vec<Result<Request, Error>>>());
    assert_eq!(0, collected.len());
}

#[test]
fn command_not_supported_response() {
    let (mut stream, _, remote, mut exec) = setup_stream_test();

    // TxLabel 4, DelayReport, which is not implemented
    assert!(remote.write(&[0x40, 0x0D, 0x40, 0x00, 0x00]).is_ok());

    let mut fut = stream.next();
    let complete = exec.run_until_stalled(&mut fut);

    // We shouldn't have received anything on the future
    assert!(complete.is_pending());

    // The peer should have responded with a Response Reject message with the
    // same TxLabel with BAD_LENGTH
    expect_remote_recv(&[0x43, 0x0D, 0x19], &remote);
}

#[test]
fn requests_are_queued_if_they_arrive_early() {
    let (stream, _, remote, mut exec) = setup_stream_test();
    assert!(remote.write(&CMD_DISCOVER).is_ok());
    let mut collected = Vec::<Request>::new();

    let mut fut = stream.try_for_each(|r| {
        collected.push(r);
        future::ready(Ok(()))
    });

    let _complete = exec.run_until_stalled(&mut fut);

    assert_eq!(1, collected.len());
}

#[test]
fn responds_with_same_tx_id() {
    let (mut stream, _, remote, mut exec) = setup_stream_test();

    assert!(remote.write(&CMD_DISCOVER).is_ok());

    let respond_res = match next_request(&mut stream, &mut exec) {
        Request::Discover { responder } => responder.send(&[]),
        // TODO: enabled when there's another request impemented
        //_ => panic!("should have received a Discover"),
    };

    assert!(respond_res.is_ok());

    expect_remote_recv(&[0x42, 0x01], &remote);
}

#[test]
fn invalid_signal_id_responds_error() {
    let (mut stream, _, remote, mut exec) = setup_stream_test();

    // This is TxLabel 4, Signal Id 0b110000, which is invalid.
    assert!(remote.write(&[0x40, 0x30]).is_ok());

    let mut fut = stream.next();
    let complete = exec.run_until_stalled(&mut fut);

    // We shouldn't have received anything on the future
    assert!(complete.is_pending());

    // The peer should have responded with a General Reject message with the
    // same TxLabel, and the same (invalid) signal identifier.
    expect_remote_recv(&[0x41, 0x30], &remote);
}

#[test]
fn discover_invalid_length() {
    let (mut stream, _, remote, mut exec) = setup_stream_test();

    // TxLabel 4, Discover, which doesn't have a payload, but we're including one.
    assert!(remote.write(&[0x40, 0x01, 0x40]).is_ok());

    let mut fut = stream.next();
    let complete = exec.run_until_stalled(&mut fut);

    // We shouldn't have received anything on the future
    assert!(complete.is_pending());

    // The peer should have responded with a Response Reject message with the
    // same TxLabel with BAD_LENGTH
    expect_remote_recv(&[0x43, 0x01, 0x11], &remote);
}

// Discovery tests

#[test]
fn discover_event_returns_results_correctly() {
    let (mut stream, _, remote, mut exec) = setup_stream_test();

    assert!(remote.write(&CMD_DISCOVER).is_ok());

    let respond_res = match next_request(&mut stream, &mut exec) {
        Request::Discover { responder } => {
            let s = StreamInformation::new(0x0A, false, MediaType::Video, EndpointType::Source);
            assert!(s.is_ok());
            responder.send(&[s.unwrap()])
        }
        // TODO: enable when we add the next request type
        //_ => panic!("should have received a Discover"),
    };

    assert!(respond_res.is_ok());

    expect_remote_recv(&[0x42, 0x01, 0x0A << 2, 0x01 << 4], &remote);
}

#[test]
fn discover_command_works() {
    let mut exec = fasync::Executor::new().expect("failed to create an executor");
    let (peer, remote) = setup_peer();
    let response_fut = peer.discover();
    pin_mut!(response_fut);
    assert!(exec.run_until_stalled(&mut response_fut).is_pending());

    let received = recv_remote(&remote).unwrap();
    // Last half of header must be Single (0b00) and Command (0b00)
    assert_eq!(0x00, received[0] & 0xF);
    assert_eq!(0x01, received[1]); // 0x01 = Discover

    let txlabel_raw = received[0] & 0xF0;

    let response: &[u8] = &[
        txlabel_raw << 4 | 0x0 << 2 | 0x2, // txlabel (same), Single (0b00), Response Accept (0b10)
        0x01,                              // Discover
        0x3E << 2 | 0x0 << 1,              // SEID (3E), Not In Use (0b0)
        0x00 << 4 | 0x1 << 3,              // Audio (0x00), Sink (0x01)
        0x01 << 2 | 0x1 << 1,              // SEID (1), In Use (0b1)
        0x01 << 4 | 0x0 << 3,              // Video (0x01), Source (0x01)
        0x17 << 2 | 0x0 << 1,              // SEID (17), Not In Use (0b0)
        0x02 << 4 | 0x1 << 3,              // Multimedia (0x02), Sink (0x01)
    ];
    assert!(remote.write(response).is_ok());

    let complete = exec.run_until_stalled(&mut response_fut);

    let endpoints = match complete {
        Poll::Ready(Ok(response)) => response,
        x => panic!("Should have a ready Ok response: {:?}", x),
    };

    assert_eq!(3, endpoints.len());
    let e1 = StreamInformation::new(0x3E, false, MediaType::Audio, EndpointType::Sink).unwrap();
    assert_eq!(e1, endpoints[0]);
    let e2 = StreamInformation::new(0x01, true, MediaType::Video, EndpointType::Source).unwrap();
    assert_eq!(e2, endpoints[1]);
    let e3 =
        StreamInformation::new(0x17, false, MediaType::Multimedia, EndpointType::Sink).unwrap();
    assert_eq!(e3, endpoints[2]);
}

#[test]
fn discover_reject_command() {
    let mut exec = fasync::Executor::new().expect("failed to create an executor");
    let (peer, remote) = setup_peer();
    let response_fut = peer.discover();
    pin_mut!(response_fut);
    assert!(exec.run_until_stalled(&mut response_fut).is_pending());

    let received = recv_remote(&remote).unwrap();
    // Last half of header must be Single (0b00) and Command (0b00)
    assert_eq!(0x00, received[0] & 0xF);
    assert_eq!(0x01, received[1]); // 0x01 = Discover

    let txlabel_raw = received[0] & 0xF0;

    let response: &[u8] = &[
        txlabel_raw | 0x0 << 2 | 0x3, // txlabel (same), Single (0b00), Response Reject (0b11)
        0x01,                         // Discover
        0x31,                         // BAD_STATE
    ];

    assert!(remote.write(response).is_ok());

    let complete = exec.run_until_stalled(&mut response_fut);

    let error = match complete {
        Poll::Ready(Err(response)) => response,
        x => panic!("Should have a ready Error response: {:?}", x),
    };

    assert_eq!(Error::RemoteRejected(0x31), error);
}

#[test]
fn exhaust_request_ids() {
    let mut exec = fasync::Executor::new().expect("failed to create an executor");
    let (peer, remote) = setup_peer();
    let mut response_futures = Vec::new();
    // There are only 16 labels, so fill up the "outgoing requests pending" buffer
    for _ in 0..16 {
        let mut response_fut = Box::pinned(peer.discover());
        assert!(exec.run_until_stalled(&mut response_fut).is_pending());
        response_futures.push(response_fut);
    }

    let mut should_fail_fut = Box::pinned(peer.discover());
    let res = exec.run_until_stalled(&mut should_fail_fut);
    assert!(res.is_ready());

    if let Poll::Ready(x) = res {
        assert!(x.is_err());
    }

    // Finish some of them.
    for _ in 0..4 {
        let received = recv_remote(&remote).unwrap();
        // Last half of header must be Single (0b00) and Command (0b00)
        assert_eq!(0x00, received[0] & 0xF);
        assert_eq!(0x01, received[1]); // 0x01 = Discover

        let txlabel_raw = received[0] & 0xF0;
        let response: &[u8] = &[
            txlabel_raw | 0x0 << 2 | 0x2, // txlabel (same), Single (0b00), Response Accept (0b10)
            0x01,                         // Discover
            0x3E << 2 | 0x0 << 1,         // SEID (3E), Not In Use (0b0)
            0x00 << 4 | 0x1 << 3,         // Audio (0x00), Sink (0x01)
        ];
        assert!(remote.write(response).is_ok());
    }

    for idx in 0..4 {
        assert!(exec
            .run_until_stalled(&mut response_futures[idx])
            .is_ready());
    }

    // We should be able to make new requests now.
    let mut another_response_fut = Box::pinned(peer.discover());
    assert!(exec
        .run_until_stalled(&mut another_response_fut)
        .is_pending());
}
