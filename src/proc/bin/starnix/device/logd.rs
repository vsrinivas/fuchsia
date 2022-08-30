// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of logd, Android's logging daemon, within the kernel.
//! TODO(fxbug.dev/96419): This is meant as a stop-gap to help debug Android binaries. Remove once
//! full init/logd support lands.

use crate::auth::FsCred;
use crate::fs::socket::{Socket, SocketAddress, SocketDomain, SocketProtocol, SocketType};
use crate::task::CurrentTask;
use crate::types::*;
use std::sync::Arc;
use std::thread;
use zerocopy::FromBytes;

const SOCKET_NAME: &[u8; 5] = b"logdw";
const LOGD_TAG: &str = "logd";

/// Creates a socket at /dev/socket/logdw and starts a thread that reads from it and emits logd log
/// messages.
pub fn create_socket_and_start_server(task: &CurrentTask) {
    let logdw_socket =
        Socket::new(SocketDomain::Unix, SocketType::Datagram, SocketProtocol::default())
            .expect("create socket");

    let devfs_root = crate::fs::devtmpfs::dev_tmp_fs(task).root().clone();
    devfs_root
        .create_node(task, b"socket", mode!(IFDIR, 0o777), DeviceType::NONE, FsCred::root())
        .expect("create /dev/socket")
        .bind_socket(
            task,
            SOCKET_NAME,
            logdw_socket.clone(),
            SocketAddress::Unix(SOCKET_NAME.to_vec()),
            mode!(IFSOCK, 0o777),
            FsCred::root(),
        )
        .expect("create /dev/socket/logdw");

    thread::spawn(move || {
        tracing::trace!(target: LOGD_TAG, "starting logd kernel daemon");
        if let Err(err) = logd_daemon(logdw_socket) {
            tracing::error!(target: LOGD_TAG, "logd kernel daemon exited with error: {:?}", err);
        }
    });
}

/// The run-loop of our logd daemon, which reads from `socket` and emits log messages.
/// This never terminates unless there is an error reading from the socket.
fn logd_daemon(socket: Arc<Socket>) -> Result<(), Errno> {
    loop {
        let messages = socket.blocking_read_kernel()?;
        for message in messages {
            if let Ok(message) = parse_logd_message(message.data.bytes()) {
                // Unfortunately, the `trace::event!` macro doesn't allow for non-const levels, so
                // we must dispatch ourselves.
                match message.level {
                    LogdLevel::Verbose => {
                        tracing::trace!(target: LOGD_TAG, tag = message.tag, tag = LOGD_TAG, tid = message.tid, message = ?message.message);
                    }
                    LogdLevel::Debug => {
                        tracing::debug!(target: LOGD_TAG, tag = message.tag, tag = LOGD_TAG, tid = message.tid, message = ?message.message);
                    }
                    LogdLevel::Info => {
                        tracing::info!(target: LOGD_TAG, tag = message.tag, tag = LOGD_TAG, tid = message.tid, message = ?message.message);
                    }
                    LogdLevel::Warn => {
                        tracing::warn!(target: LOGD_TAG, tag = message.tag, tag = LOGD_TAG, tid = message.tid, message = ?message.message);
                    }
                    LogdLevel::Error => {
                        tracing::error!(target: LOGD_TAG, tag = message.tag, tag = LOGD_TAG, tid = message.tid, message = ?message.message);
                    }
                    LogdLevel::Fatal => {
                        tracing::error!(target: LOGD_TAG, tag = message.tag, tag = LOGD_TAG, tid = message.tid, FATAL = true, message = ?message.message);
                    }
                }
            }
        }
    }
}

/// Parses a logd message from the bytes read from the /dev/socket/logdw socket.
fn parse_logd_message(message: &[u8]) -> Result<LogdMessage<'_>, Errno> {
    // Need enough space for at least the header.
    if message.len() < std::mem::size_of::<android_log_header_t>() {
        return error!(EINVAL);
    }

    // Split the message into header and payload.
    let (header_bytes, payload_bytes) =
        message.split_at(std::mem::size_of::<android_log_header_t>());

    let header = android_log_header_t::read_from(header_bytes).ok_or_else(|| errno!(EINVAL))?;
    parse_logd_payload(header.tid as pid_t, payload_bytes)
}

/// Parses the payload of the logd message. This contains the log level, tag, and actual message.
fn parse_logd_payload(tid: pid_t, message: &[u8]) -> Result<LogdMessage<'_>, Errno> {
    if message.is_empty() {
        return error!(ENOMEM);
    }
    let level = match message[0] {
        2 => LogdLevel::Verbose,
        3 => LogdLevel::Debug,
        4 => LogdLevel::Info,
        5 => LogdLevel::Warn,
        6 => LogdLevel::Error,
        7 => LogdLevel::Fatal,
        _ => return error!(EINVAL),
    };
    let message = &message[1..];
    let tag_end = memchr::memchr(0, message).ok_or_else(|| errno!(EINVAL))?;
    let tag = std::str::from_utf8(&message[..tag_end]).map_err(|_| errno!(EINVAL))?;
    let message = &message[tag_end + 1..];
    let message_end = memchr::memchr(0, message).ok_or_else(|| errno!(EINVAL))?;
    let message = std::str::from_utf8(&message[..message_end]).map_err(|_| errno!(EINVAL))?;
    Ok(LogdMessage { level, tid, tag, message })
}

/// Re-define of Android's userspace `android_log_header_t` C-struct.
#[derive(Debug, Clone, Copy, FromBytes)]
#[repr(C, packed)]
struct android_log_header_t {
    /// The log buffer ID.
    id: u8,
    /// The sender's thread ID.
    tid: u16,
    /// The time at which the log was generated.
    realtime: log_time,
}

/// Re-define of Android's userspace `log_time` C-struct.
#[derive(Debug, Clone, Copy, FromBytes)]
#[repr(C, packed)]
struct log_time {
    tv_sec: u32,
    tv_nsec: u32,
}

/// Android's log levels.
enum LogdLevel {
    Verbose,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
}

/// A parsed message received from the logd socket.
struct LogdMessage<'a> {
    /// The level at which to log, according to Android's logd levels.
    level: LogdLevel,
    /// The thread ID that sent the log.
    tid: pid_t,
    /// The tag the log should be tagged with.
    tag: &'a str,
    /// The actual message in the log.
    message: &'a str,
}
