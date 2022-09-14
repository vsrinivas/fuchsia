// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_async::Task;
use overnet_core::{
    log_errors, ConnectionId, Endpoint, LinkReceiver, LinkSender, MAX_FRAME_LENGTH,
};
use quic::AsyncConnection;
use std::sync::Arc;

/// Send half for QUIC links
pub struct QuicSender {
    quic: Arc<AsyncConnection>,
}

/// Receive half for QUIC links
pub struct QuicReceiver {
    quic: Arc<AsyncConnection>,
    // Processing loop for the link - once there's no receiver this can stop
    _task: Task<()>,
}

/// Create a QUIC link to tunnel an Overnet link through.
pub async fn new_quic_link(
    sender: LinkSender,
    receiver: LinkReceiver,
    endpoint: Endpoint,
) -> Result<(QuicSender, QuicReceiver, ConnectionId), Error> {
    let scid = ConnectionId::new();
    let mut config = sender.router().new_quiche_config().await?;
    config.set_application_protos(b"\x10overnet.link/0.2")?;
    config.set_initial_max_data(0);
    config.set_initial_max_stream_data_bidi_local(0);
    config.set_initial_max_stream_data_bidi_remote(0);
    config.set_initial_max_stream_data_uni(0);
    config.set_initial_max_streams_bidi(0);
    config.set_initial_max_streams_uni(0);
    config.set_max_send_udp_payload_size(16384);
    config.set_max_recv_udp_payload_size(16384);
    const DGRAM_QUEUE_SIZE: usize = 1024 * 1024;
    config.enable_dgram(true, DGRAM_QUEUE_SIZE, DGRAM_QUEUE_SIZE);

    let quic = match endpoint {
        Endpoint::Client => AsyncConnection::connect(
            None,
            &quiche::ConnectionId::from_ref(&scid.to_array()),
            receiver
                .peer_node_id()
                .map(|x| x.to_ipv6_repr())
                .unwrap_or_else(|| "127.0.0.1:65535".parse().unwrap()),
            &mut config,
        )?,
        Endpoint::Server => AsyncConnection::accept(
            &quiche::ConnectionId::from_ref(&scid.to_array()),
            receiver
                .peer_node_id()
                .map(|x| x.to_ipv6_repr())
                .unwrap_or_else(|| "127.0.0.1:65535".parse().unwrap()),
            &mut config,
        )?,
    };

    Ok((
        QuicSender { quic: quic.clone() },
        QuicReceiver {
            _task: Task::spawn(log_errors(
                run_link(sender, receiver, quic.clone()),
                "QUIC link failed",
            )),
            quic,
        },
        scid,
    ))
}

impl QuicReceiver {
    /// Report a packet was received.
    /// An error processing a packet does not indicate that the link should be closed.
    pub async fn received_frame(&self, packet: &mut [u8]) {
        if let Err(e) = self.quic.recv(packet).await {
            tracing::warn!("error receiving packet: {:?}", e);
        }
    }
}

impl QuicSender {
    /// Fetch the next frame that should be sent by the link. Returns Ok(None) on link
    /// closure, Ok(Some(packet_length)) on successful read, and an error otherwise.
    pub async fn next_send(&self, frame: &mut [u8]) -> Result<Option<usize>, Error> {
        self.quic.next_send(frame).await
    }
}

async fn run_link(
    sender: LinkSender,
    receiver: LinkReceiver,
    quic: Arc<AsyncConnection>,
) -> Result<(), Error> {
    futures::future::try_join(
        link_to_quic(sender, quic.clone()),
        quic_to_link(receiver, quic.clone()),
    )
    .await?;
    Ok(())
}

async fn link_to_quic(mut link: LinkSender, quic: Arc<AsyncConnection>) -> Result<(), Error> {
    while let Some(mut p) = link.next_send().await {
        p.drop_inner_locks();
        quic.dgram_send(p.bytes_mut()).await?;
    }
    Ok(())
}

async fn quic_to_link(mut link: LinkReceiver, quic: Arc<AsyncConnection>) -> Result<(), Error> {
    let mut frame = [0u8; MAX_FRAME_LENGTH];

    loop {
        let n = quic.dgram_recv(&mut frame).await?;
        link.received_frame(&mut frame[..n]).await
    }
}
