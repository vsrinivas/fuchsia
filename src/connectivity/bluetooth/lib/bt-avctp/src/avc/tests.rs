// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::Channel,
    fuchsia_zircon::{self as zx, Status},
    futures::executor::block_on,
    std::result,
    std::task::Poll,
};

use super::*;
use crate::avctp::MessageType as AvctpMessageType;

#[test]
fn closes_channel_when_dropped() {
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
#[should_panic(expected = "Command stream has already been taken")]
fn can_only_take_stream_once() {
    let mut _exec = fasync::Executor::new().expect("failed to create an executor");
    let (_, control) = Channel::create();

    let peer = Peer::new(control);
    let mut _stream = peer.take_command_stream();
    let mut _stream2 = peer.take_command_stream();
}

pub(crate) fn setup_peer() -> (Peer, Channel) {
    let (remote, control) = Channel::create();

    let peer = Peer::new(control);
    (peer, remote)
}

fn setup_stream_test() -> (CommandStream, Peer, Channel, fasync::Executor) {
    let exec = fasync::Executor::new().expect("failed to create an executor");
    let (peer, remote) = setup_peer();
    let stream = peer.take_command_stream();
    (stream, peer, remote, exec)
}

pub(crate) fn recv_remote(remote: &Channel) -> result::Result<Vec<u8>, zx::Status> {
    let waiting = remote.as_ref().outstanding_read_bytes().expect("bytes ready");
    let mut response: Vec<u8> = vec![0; waiting];
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
fn closed_peer_ends_request_stream() {
    let (stream, _, _, _) = setup_stream_test();
    let collected = block_on(stream.collect::<Vec<Result<Command>>>());
    assert_eq!(0, collected.len());
}

#[test]
fn send_stop_avc_passthrough_command_timeout() {
    let (_stream, peer, channel, mut exec) = setup_stream_test();
    let mut cmd_fut = Box::pin(peer.send_avc_passthrough_command(&[69, 0]));
    let poll_ret: Poll<Result<CommandResponse>> = exec.run_until_stalled(&mut cmd_fut);
    assert!(poll_ret.is_pending());

    expect_remote_recv(
        &[
            0x00, // TxLabel 0, Single 0, Command 0, Ipid 0,
            0x11, // AV PROFILE
            0x0e, // AV PROFILE
            0x00, // command: Control
            0x48, // panel subunit_type 9 (<< 3), subunit_id 0
            0x7c, // op code: passthrough
            0x45, // random keypress
            0x00, // passthrough payload
        ],
        &channel,
    );

    exec.wake_next_timer();
    assert_eq!(Poll::Ready(Err(Error::Timeout)), exec.run_until_stalled(&mut cmd_fut));
}

#[test]
fn send_stop_avc_passthrough_command() {
    let (_stream, peer, channel, mut exec) = setup_stream_test();
    let mut cmd_fut = Box::pin(peer.send_avc_passthrough_command(&[69, 0]));
    let poll_ret: Poll<Result<CommandResponse>> = exec.run_until_stalled(&mut cmd_fut);
    assert!(poll_ret.is_pending());

    expect_remote_recv(
        &[
            0x00, // TxLabel 0, Single 0, Command 0, Ipid 0,
            0x11, // AV PROFILE
            0x0e, // AV PROFILE
            0x00, // command: Control
            0x48, // panel subunit_type 9 (<< 3), subunit_id 0
            0x7c, // op code: passthrough
            0x45, // random keypress
            0x00, // passthrough payload
        ],
        &channel,
    );

    let write_buf = &[
        0x02, // TxLabel 0, Single 0, Response 1, Ipid 0,
        0x11, // AV PROFILE
        0x0e, // AV PROFILE
        0x09, // response: Accepted
        0x48, // panel subunit_type 9 (<< 3), subunit_id 0
        0x7c, // op code: passthrough
        0x45, // random keypress
        0x00, // passthrough payload
    ];

    assert!(channel.as_ref().write(write_buf).is_ok()); // Response accept packet
    let poll_ret = exec.run_until_stalled(&mut cmd_fut);
    let command_response = match poll_ret {
        Poll::Ready(Ok(response)) => response,
        x => panic!("Should have had an Ready OK response and got {:?}", x),
    };
    assert_eq!(ResponseType::Accepted, command_response.0);
}

#[test]
fn receive_register_notification_command() {
    let (mut stream, _peer, channel, mut exec) = setup_stream_test();
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
    assert!(channel.as_ref().write(notif_command_packet).is_ok());
    let command = next_request(&mut stream, &mut exec);
    assert!(command.avctp_header().is_type(&AvctpMessageType::Command));
    assert!(command.avctp_header().is_single());
    assert_eq!(PacketType::Command(CommandType::Notify), command.avc_header().packet_type()); // NOTIFY
    assert_eq!(&OpCode::VendorDependent, command.avc_header().op_code());
    assert_eq!(Some(SubunitType::Panel), command.avc_header().subunit_type());
    assert_eq!(
        &[
            // vendor specific payload (register notification for volume change)
            0x31, // register notification Pdu_ID
            0x00, // reserved/packet type
            0x00, 0x05, // parameter len
            0x0D, // Event ID
            0x00, 0x00, 0x00, 0x00, // Playback interval
        ],
        command.body(),
    );
    assert!(command.send_response(ResponseType::NotImplemented, &[]).is_ok());
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
        &channel,
    );
}

#[test]
fn receive_unit_info() {
    let (mut stream, _peer, channel, mut exec) = setup_stream_test();
    let command_packet = &[
        0x00, // TxLabel 0, Single 0, Command 0, Ipid 0,
        0x11, // AV PROFILE
        0x0e, // AV PROFILE
        0x01, // command: Status
        0xff, // unit subunit_type 0x1F (<< 3), subunit_id 7
        0x30, // opcode: unit info
        0xff, 0xff, 0xff, 0xff, 0xff, // pad
    ];
    assert!(channel.as_ref().write(command_packet).is_ok());

    let mut fut = stream.next();
    let complete = exec.run_until_stalled(&mut fut); // wake and pump.
    assert!(complete.is_pending());

    expect_remote_recv(
        &[
            0x02, // TxLabel 0, Single 0, response 1, Ipid 0,
            0x11, // AV PROFILE
            0x0e, // AV PROFILE
            0x0c, // Response: stable
            0xff, // unit subunit_type 0x1F (<< 3), subunit_id 7
            0x30, // opcode: unit info
            0x07, // constant
            0x48, // SubunitType::Panel
            0xff, 0xff, 0xff, // generic company ID.
        ],
        &channel,
    );
}

#[test]
fn receive_subunit_info() {
    let (mut stream, _peer, channel, mut exec) = setup_stream_test();
    let command_packet = &[
        0x00, // TxLabel 0, Single 0, Command 0, Ipid 0,
        0x11, // AV PROFILE
        0x0e, // AV PROFILE
        0x01, // command: Status
        0xff, // unit subunit_type 0x1F (<< 3), subunit_id 7
        0x31, // opcode: sub_unit info
        0x07, // extension code
        0xff, 0xff, 0xff, 0xff, // pad
    ];
    assert!(channel.as_ref().write(command_packet).is_ok());

    let mut fut = stream.next();
    let complete = exec.run_until_stalled(&mut fut); // wake and pump.
    assert!(complete.is_pending());

    expect_remote_recv(
        &[
            0x02, // TxLabel 0, Single 0, response 1, Ipid 0,
            0x11, // AV PROFILE
            0x0e, // AV PROFILE
            0x0c, // Response: stable
            0xff, // unit subunit_type 0x1F (<< 3), subunit_id 7
            0x31, // opcode: sub unit info
            0x07, // extension code
            0x48, // SubunitType::Panel
            0xff, 0xff, 0xff, // padding
        ],
        &channel,
    );
}
