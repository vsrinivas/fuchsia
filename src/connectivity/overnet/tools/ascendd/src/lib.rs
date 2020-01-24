// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_overnet_protocol::StreamSocketGreeting;
use futures::prelude::*;
use overnet_core::{log_errors, spawn, Router, RouterOptions, StreamDeframer, StreamFramer};
use std::rc::Rc;
use tokio::io::AsyncRead;

async fn read_incoming(
    stream: tokio::io::ReadHalf<tokio::net::UnixStream>,
    mut chan: futures::channel::mpsc::Sender<Vec<u8>>,
) -> Result<(), Error> {
    let mut buf = [0u8; 1024];
    let mut stream = Some(stream);
    let mut deframer = StreamDeframer::new();

    loop {
        let rd = tokio::io::read(stream.take().unwrap(), &mut buf[..]);
        let rd = futures::compat::Compat01As03::new(rd);
        let (returned_stream, _, n) = rd.await?;
        if n == 0 {
            return Ok(());
        }
        stream = Some(returned_stream);
        deframer.queue_recv(&mut buf[..n]);
        while let Some(frame) = deframer.next_incoming_frame() {
            chan.send(frame).await?;
        }
    }
}

async fn process_incoming(
    node: Rc<Router>,
    mut rx_frames: futures::channel::mpsc::Receiver<Vec<u8>>,
    tx_bytes: tokio::io::WriteHalf<tokio::net::UnixStream>,
) -> Result<(), Error> {
    let node_id = node.node_id();

    // Send first frame
    let mut framer = StreamFramer::new();
    let mut greeting = StreamSocketGreeting {
        magic_string: Some(hoist::ASCENDD_SERVER_CONNECTION_STRING.to_string()),
        node_id: Some(node_id.into()),
        connection_label: Some("ascendd".to_string()),
    };
    let mut bytes = Vec::new();
    let mut handles = Vec::new();
    fidl::encoding::Encoder::encode(&mut bytes, &mut handles, &mut greeting)?;
    assert_eq!(handles.len(), 0);
    framer.queue_send(bytes.as_slice())?;
    let send = framer.take_sends();
    let wr = tokio::io::write_all(tx_bytes, send);
    let wr = futures::compat::Compat01As03::new(wr).map_err(|e| -> Error { e.into() });

    let first_frame = rx_frames
        .next()
        .map(|r| r.ok_or_else(|| anyhow::format_err!("Stream closed before greeting received")));
    let (mut frame, (tx_bytes, _)) = futures::try_join!(first_frame, wr)?;
    let mut greeting = StreamSocketGreeting::empty();
    // WARNING: Since we are decoding without a transaction header, we have to
    // provide a context manually. This could cause problems in future FIDL wire
    // format migrations, which are driven by header flags.
    let context = fidl::encoding::Context {};
    fidl::encoding::Decoder::decode_with_context(&context, frame.as_mut(), &mut [], &mut greeting)?;

    let node_id = match greeting {
        StreamSocketGreeting { magic_string: None, .. } => anyhow::bail!(
            "Required magic string '{}' not present in greeting",
            hoist::ASCENDD_CLIENT_CONNECTION_STRING
        ),
        StreamSocketGreeting { magic_string: Some(ref x), .. }
            if x != hoist::ASCENDD_CLIENT_CONNECTION_STRING =>
        {
            anyhow::bail!(
                "Expected magic string '{}' in greeting, got '{}'",
                hoist::ASCENDD_CLIENT_CONNECTION_STRING,
                x
            )
        }
        StreamSocketGreeting { node_id: None, .. } => anyhow::bail!("No node id in greeting"),
        StreamSocketGreeting { node_id: Some(n), .. } => n.id,
    };

    // Register our new link!
    let link_receiver = node.new_link(node_id.into()).await?;
    let link_sender = link_receiver.clone();
    spawn(log_errors(
        async move {
            let mut tx_bytes = Some(tx_bytes);
            let mut buf = [0u8; 4096];
            while let Some(n) = link_sender.next_send(&mut buf).await? {
                framer.queue_send(&buf[..n])?;
                let wr = tokio::io::write_all(tx_bytes.take().unwrap(), framer.take_sends());
                let wr = futures::compat::Compat01As03::new(wr).map_err(|e| -> Error { e.into() });
                let (t, _) = wr.await?;
                tx_bytes = Some(t);
            }
            Ok(())
        },
        "Writing to Ascendd socket failed",
    ));

    // Supply node with incoming frames
    while let Some(mut frame) = rx_frames.next().await {
        if let Err(err) = link_receiver.received_packet(frame.as_mut()).await {
            log::trace!("Failed handling packet: {:?}", err);
        }
    }

    Ok(())
}

pub async fn run_ascendd(sockpath: String) -> Result<(), Error> {
    log::info!("[log] starting ascendd");
    hoist::logger::init()?;
    let _ = std::fs::remove_file(&sockpath);

    let incoming = tokio::net::UnixListener::bind(&sockpath)?.incoming();
    let mut incoming = futures::compat::Compat01As03::new(incoming);
    log::info!("ascendd listening to socket {}", sockpath);

    let node = Router::new(
        RouterOptions::new()
            .set_quic_server_key_file(hoist::hard_coded_server_key().unwrap())
            .set_quic_server_cert_file(hoist::hard_coded_server_cert().unwrap())
            .export_diagnostics(fidl_fuchsia_overnet_protocol::Implementation::Ascendd),
    )?;

    while let Some(stream) = incoming.next().await {
        let stream = stream?;
        let (rx_bytes, tx_bytes) = stream.split();
        let (tx_frames, rx_frames) = futures::channel::mpsc::channel(1);
        spawn(log_errors(
            read_incoming(rx_bytes, tx_frames),
            "Failed reading bytes from Ascendd socket",
        ));
        spawn(log_errors(
            process_incoming(node.clone(), rx_frames, tx_bytes),
            "Failed processing Ascendd socket",
        ));
    }

    Ok(())
}
