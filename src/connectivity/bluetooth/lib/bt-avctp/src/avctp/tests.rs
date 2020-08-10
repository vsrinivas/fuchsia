// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, Status},
    futures::{executor::block_on, StreamExt},
    std::result,
};

use super::*;

pub(crate) fn setup_peer() -> (Peer, Channel) {
    let (remote, signaling) = Channel::create();

    let peer = Peer::new(signaling);
    (peer, remote)
}

fn setup_stream_test() -> (CommandStream, Peer, Channel, fasync::Executor) {
    let exec = fasync::Executor::new().expect("failed to create an executor");
    let (peer, remote) = setup_peer();
    let stream = peer.take_command_stream();
    (stream, peer, remote, exec)
}

pub(crate) fn recv_remote(remote: &Channel) -> result::Result<Vec<u8>, zx::Status> {
    let waiting = remote.as_ref().outstanding_read_bytes();
    assert!(waiting.is_ok());
    let mut response: Vec<u8> = vec![0; waiting.unwrap()];
    let response_read = remote.as_ref().read(response.as_mut_slice())?;
    assert_eq!(response.len(), response_read);
    Ok(response)
}

pub(crate) fn expect_remote_recv(expected: &[u8], remote: &Channel) {
    let r = recv_remote(&remote);
    assert!(r.is_ok());
    let response = r.unwrap();
    if expected.len() != response.len() {
        panic!("received wrong length\nexpected: {:?}\nreceived: {:?}", expected, response);
    }
    assert_eq!(expected, &response[0..expected.len()]);
}

fn next_request(stream: &mut CommandStream, exec: &mut fasync::Executor) -> Command {
    let mut fut = stream.next();
    let complete = exec.run_until_stalled(&mut fut);

    match complete {
        Poll::Ready(Some(Ok(r))) => r,
        _ => panic!("should have a request"),
    }
}

#[test]
fn closes_socket_when_dropped() {
    let mut _exec = fasync::Executor::new().expect("failed to create an executor");
    let (peer_chan, control) = Channel::create();

    {
        let peer = Peer::new(control);
        let mut _stream = peer.take_command_stream();
    }

    // Writing to the sock from the other end should fail.
    let write_res = peer_chan.as_ref().write(&[0; 1]);
    assert!(write_res.is_err());
    assert_eq!(Status::PEER_CLOSED, write_res.err().unwrap());
}

#[test]
fn socket_open_when_stream_open() {
    let mut _exec = fasync::Executor::new().expect("failed to create an executor");
    let (peer_chan, control) = Channel::create();

    {
        let mut _stream;
        {
            let peer = Peer::new(control);
            _stream = peer.take_command_stream();
        }

        // Writing to the sock from the other end should pass.
        let write_res = peer_chan.as_ref().write(&[0; 1]);
        assert!(write_res.is_ok());
    }

    // Writing to the sock from the other end should fail.
    let write_res = peer_chan.as_ref().write(&[0; 1]);
    assert!(write_res.is_err());
    assert_eq!(Status::PEER_CLOSED, write_res.err().unwrap());
}

#[test]
#[should_panic(expected = "Command stream has already been taken")]
fn can_only_take_stream_once() {
    let mut _exec = fasync::Executor::new().expect("failed to create an executor");
    let (_, control) = Channel::create();

    let peer = Peer::new(control);
    let mut _stream = peer.take_command_stream();
    let mut _stream2 = peer.take_command_stream();
}

#[test]
fn closed_peer_ends_request_stream() {
    let (stream, _, _, _) = setup_stream_test();
    let collected = block_on(stream.collect::<Vec<Result<Command>>>());
    assert_eq!(0, collected.len());
}

#[test]
fn send_command_receive_response() {
    let (_stream, peer, socket, mut exec) = setup_stream_test();

    // sending random payload.
    let mut command_stream =
        peer.send_command(&[22, 33, 44, 55]).expect("Unable to get command stream");

    // Assuming we got assigned TxLabel(0) here. This might be flaky.
    // Sending random payload.
    expect_remote_recv(
        &[
            0x00, // TxLabel 0, Single 0, Command 0, Ipid 0,
            0x11, // AV PROFILE
            0x0e, // AV PROFILE
            22, 33, 44, 55, // Random payload should match above
        ],
        &socket,
    );

    let mut response_fut = command_stream.next();
    let stream_ret: Poll<Option<Result<Packet>>> = exec.run_until_stalled(&mut response_fut);
    assert!(stream_ret.is_pending());
    assert!(socket
        .as_ref()
        .write(&[
            0x02, // TxLabel 0, Single 0, Response 1, Ipid 0,
            0x11, // AV PROFILE
            0x0e, // AV PROFILE
            66, 77, 88, 99 // Random Payload
        ])
        .is_ok()); // Response accept packet

    let stream_ret: Poll<Option<Result<Packet>>> = exec.run_until_stalled(&mut response_fut);
    assert!(stream_ret.is_ready());
    if let Poll::Ready(Some(Ok(packet))) = stream_ret {
        // Random Payload should match what we expected
        assert_eq!(&[66, 77, 88, 99], packet.body());
        assert!(packet.header().is_single());
        assert!(packet.header().is_type(&MessageType::Response));
        assert_eq!(&TxLabel::try_from(0).unwrap(), packet.header().label());
    } else {
        panic!("Invalid stream result");
    }
}

#[test]
fn receive_command_send_response() {
    let (mut stream, _peer, socket, mut exec) = setup_stream_test();
    let notif_command_packet = &[
        0x00, // TxLabel 0, Single 0, Command 0, Ipid 0,
        0x11, // AV PROFILE
        0x0e, // AV PROFILE
        0x03, // command: Notify
        0x48, // panel subunit_type 9 (<< 3), subunit_id 0
        0x00, // op code: VendorDependent
        0x00, 0x19, 0x58, // bit sig company id
        // vendor specific payload (register notification for volume change)
        0x31, // register notification Pdu_ID
        0x00, // reserved/packet type
        0x00, 0x05, // parameter len
        0x0D, // Event ID
        0x00, 0x00, 0x00, 0x00, // Playback interval
    ];
    assert!(socket.as_ref().write(notif_command_packet).is_ok());
    let command = next_request(&mut stream, &mut exec);
    assert!(command.header().is_type(&MessageType::Command));
    assert!(command.header().is_single());
    assert_eq!(
        // body should match the same payload above
        &[
            0x03, // command: Notify
            0x48, // panel subunit_type 9 (<< 3), subunit_id 0
            0x00, // op code: VendorDependent
            0x00, 0x19, 0x58, // bit sig company id
            // vendor specific payload (register notification for volume change)
            0x31, // register notification Pdu_ID
            0x00, // reserved/packet type
            0x00, 0x05, // parameter len
            0x0D, // Event ID
            0x00, 0x00, 0x00, 0x00, // Playback interval
        ],
        command.body()
    );
    assert!(command
        .send_response(&[
            0x08, // response: NotImplemented
            0x48, // panel subunit_type 9 (<< 3), subunit_id 0
            0x00, // op code: VendorDependent
            0x00, 0x19, 0x58, // bit sig company id
        ],)
        .is_ok());
    expect_remote_recv(
        &[
            0x02, // TxLabel 0, Single 0, Response 1, Ipid 0,
            0x11, // AV PROFILE
            0x0e, // AV PROFILE
            0x08, // response: NotImplemented
            0x48, // panel subunit_type 9 (<< 3), subunit_id 0
            0x00, // op code: VendorDependent
            0x00, 0x19, 0x58, // bit sig company id
        ],
        &socket,
    );
}

#[test]
fn receive_command_too_short_is_dropped() {
    let (mut stream, _peer, socket, mut exec) = setup_stream_test();
    let notif_command_packet = &[
        // No payload. Only a command
        0x00, // TxLabel 0, Single 0, Command 0, Ipid 0,
        0x11, // AV PROFILE
        0x0e, // AV PROFILE
    ];
    assert!(socket.as_ref().write(notif_command_packet).is_ok());

    let mut fut = stream.next();
    let complete = exec.run_until_stalled(&mut fut);
    assert!(complete.is_pending());
}

#[test]
fn receive_invalid_is_dropped() {
    let (mut stream, _peer, socket, mut exec) = setup_stream_test();
    let notif_command_packet = &[0];
    assert!(socket.as_ref().write(notif_command_packet).is_ok());

    let mut fut = stream.next();
    let complete = exec.run_until_stalled(&mut fut);
    assert!(complete.is_pending());
}

#[test]
fn invalid_profile_id_response() {
    let (mut stream, _peer, socket, mut exec) = setup_stream_test();
    let notif_command_packet = &[
        // command for wrong profile id
        0x03, // TxLabel 0, Single 0, Response 1, Ipid 1,
        0x11, 0x00, // random profile ID
        3, 72, 0, // random payload
    ];
    assert!(socket.as_ref().write(notif_command_packet).is_ok());

    let mut fut = stream.next();
    let complete = exec.run_until_stalled(&mut fut); // wake and pump.
    assert!(complete.is_pending());

    expect_remote_recv(
        &[
            // Ipid bit should be set.
            0x03, // TxLabel 0, Single 0, Response 1, Ipid 1,
            0x11, 0x00, // random profile ID same as above
        ],
        &socket,
    ); // receive invalid profile response
}
