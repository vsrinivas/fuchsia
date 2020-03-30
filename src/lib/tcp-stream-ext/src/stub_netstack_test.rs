// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    matches::assert_matches,
    std::{
        net::{Ipv4Addr, SocketAddr, TcpListener, TcpStream},
        os::unix::io::{FromRawFd, IntoRawFd as _},
    },
    tcp_stream_ext::TcpStreamExt as _,
};

const TCP_KEEPCNT_OPTION_VALUE: i32 = -11;
const TCP_KEEPINTVL_OPTION_VALUE: i32 = -12;
const TCP_USER_TIMEOUT_OPTION_VALUE: i32 = -13;

fn make_tcp_stream() -> TcpStream {
    let listener = TcpListener::bind(&SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0)).unwrap();
    let fd = listener.into_raw_fd();
    // Making a `TcpStream` by converting the listener instead of `TcpStream::connect`ing to it
    // allows implementing less of fuchsia.posix.socket in the stub (notably, `GetSockName`).
    // Safe because the fd is owned.
    unsafe { TcpStream::from_raw_fd(fd) }
}

#[test]
fn keepalive_interval_errors_on_negative_duration() {
    let stream = make_tcp_stream();
    assert_matches!(
        stream.keepalive_interval(),
        Err(tcp_stream_ext::Error::NegativeDuration(TCP_KEEPINTVL_OPTION_VALUE))
    );
}

#[test]
fn user_timeout_errors_on_negative_duration() {
    let stream = make_tcp_stream();
    assert_matches!(
        stream.user_timeout(),
        Err(tcp_stream_ext::Error::NegativeDuration(TCP_USER_TIMEOUT_OPTION_VALUE))
    );
}

#[test]
fn keepalive_count() {
    let stream = make_tcp_stream();
    assert_matches!(stream.keepalive_count(), Ok(TCP_KEEPCNT_OPTION_VALUE));
}
