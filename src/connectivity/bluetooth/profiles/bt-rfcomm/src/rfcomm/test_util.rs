// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::Channel,
    futures::{channel::mpsc, task::Poll, StreamExt},
    packet_encoding::Encodable,
};

use crate::rfcomm::frame::{mux_commands::MuxCommandMarker, Frame, FrameData, UIHData, UserData};
use crate::rfcomm::types::DLCI;

/// Simulates the peer sending an RFCOMM frame over the L2CAP `remote` socket.
#[track_caller]
pub fn send_peer_frame(remote: &fidl::Socket, frame: Frame) {
    let mut buf = vec![0; frame.encoded_len()];
    assert!(frame.encode(&mut buf[..]).is_ok());
    assert!(remote.write(&buf).is_ok());
}

/// Expects a frame to be received by the peer. This expectation does not validate the
/// contents of the data received.
#[track_caller]
pub fn expect_frame_received_by_peer(exec: &mut fasync::Executor, remote: &mut Channel) {
    let mut vec = Vec::new();
    let mut remote_fut = Box::pin(remote.read_datagram(&mut vec));
    assert!(exec.run_until_stalled(&mut remote_fut).is_ready());
}

#[track_caller]
pub fn expect_pending<T: std::fmt::Debug>(
    exec: &mut fasync::Executor,
    receiver: &mut mpsc::Receiver<T>,
) {
    let mut stream = Box::pin(receiver.next());
    match exec.run_until_stalled(&mut stream) {
        Poll::Pending => {}
        x => panic!("Expected pending but got {:?}", x),
    }
}

#[track_caller]
pub fn expect_ready<T: std::fmt::Debug>(
    exec: &mut fasync::Executor,
    receiver: &mut mpsc::Receiver<T>,
) -> T {
    let mut stream = Box::pin(receiver.next());
    match exec.run_until_stalled(&mut stream) {
        Poll::Ready(Some(item)) => item,
        x => panic!("Expected ready but got {:?}", x),
    }
}

/// Expects the provided `expected` frame data on the `receiver`.
#[track_caller]
pub fn expect_frame(
    exec: &mut fasync::Executor,
    receiver: &mut mpsc::Receiver<Frame>,
    expected: FrameData,
    dlci: Option<DLCI>,
) {
    let frame = expect_ready(exec, receiver);
    assert_eq!(frame.data, expected);
    if let Some(dlci) = dlci {
        assert_eq!(frame.dlci, dlci);
    }
}

/// Expects the `expected` UserData with potential `credits` on the `receiver`.
#[track_caller]
pub fn expect_user_data_frame(
    exec: &mut fasync::Executor,
    receiver: &mut mpsc::Receiver<Frame>,
    expected: UserData,
    expected_credits: Option<u8>,
) {
    let frame = expect_ready(exec, receiver);
    assert_eq!(frame.data, FrameData::UnnumberedInfoHeaderCheck(UIHData::User(expected)));
    assert_eq!(frame.credits, expected_credits);
}

/// Expects the provided `expected` MuxCommand on the `receiver`.
#[track_caller]
pub fn expect_mux_command(
    exec: &mut fasync::Executor,
    receiver: &mut mpsc::Receiver<Frame>,
    expected: MuxCommandMarker,
) {
    let frame = expect_ready(exec, receiver);
    if let FrameData::UnnumberedInfoHeaderCheck(UIHData::Mux(mux_command)) = frame.data {
        assert_eq!(mux_command.params.marker(), expected);
    } else {
        panic!("Expected MuxCommand but got: {:?}", frame.data);
    }
}
