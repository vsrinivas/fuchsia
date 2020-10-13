// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use archivist_lib::logs::message::fx_log_packet_t;
use fidl_fuchsia_logger::{LogLevelFilter, LogSinkMarker};
use fuchsia_zircon as zx;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let (send, sink) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();

    for (message, severity) in &[
        (&b"crasher has initialized"[..], LogLevelFilter::Info),
        (&b"crasher is approaching the crash"[..], LogLevelFilter::Warn),
        (&b"oh no we're crashing"[..], LogLevelFilter::Error),
    ] {
        let mut packet: fx_log_packet_t = Default::default();
        packet.metadata.pid = 1;
        packet.metadata.tid = 1;
        packet.metadata.severity = severity.into_primitive() as i32;
        packet.metadata.dropped_logs = 0;
        packet.data[0] = 0;
        packet.add_data(1, message);
        send.write(packet.as_bytes()).unwrap();
    }

    fuchsia_component::client::connect_to_service::<LogSinkMarker>()
        .unwrap()
        .connect(sink)
        .unwrap();
    panic!("This is an expected panic, hopefully our log messages made it out!");
}
