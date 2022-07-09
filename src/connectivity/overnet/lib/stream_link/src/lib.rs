// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Controls one link to another node over a zx socket.

// TODO(fxb/104019): Consider enabling globally.
#![deny(unused_crate_dependencies)]

use {
    anyhow::Error,
    byteorder::WriteBytesExt,
    futures::prelude::*,
    overnet_core::{ConfigProducer, LinkIntroductionFacts, Router},
    std::sync::Arc,
    stream_framer::{Deframed, Deframer, Format, Framer, ReadBytes},
};

pub fn run_stream_link<'a>(
    node: Arc<Router>,
    rx_bytes: &'a mut (dyn AsyncRead + Unpin + Send),
    tx_bytes: &'a mut (dyn AsyncWrite + Unpin + Send),
    introduction_facts: LinkIntroductionFacts,
    config: ConfigProducer,
) -> impl 'a + Send + Future<Output = Result<(), Error>> {
    let (mut link_sender, mut link_receiver) = node.new_link(introduction_facts, config);
    drop(node);

    let framer = Framer::new(LosslessBinary);
    let mut deframer = Deframer::new(LosslessBinary);

    futures::future::try_join(
        async move {
            while let Some(frame) = link_sender.next_send().await {
                let msg = framer.write_frame(frame.bytes())?;
                tx_bytes.write_all(&msg).await?;
                tx_bytes.flush().await?;
            }
            Ok(())
        },
        async move {
            let mut buf = [0; 4096];
            loop {
                let n = rx_bytes.read(&mut buf).await?;

                // If we've reached the end of the stream, then we can exit early. Any left over
                // data in the deframer would be unframed, which we don't care about.
                if n == 0 {
                    return Ok(());
                }

                for frame in deframer.parse_frames(&buf[..n]) {
                    if let ReadBytes::Framed(mut frame) = frame? {
                        link_receiver.received_frame(&mut frame).await;
                    }
                }
            }
        },
    )
    .map_ok(|((), ())| ())
}

/// Framing format that assumes a lossless underlying byte stream that can transport all 8 bits of a
/// byte.
struct LosslessBinary;

impl Format for LosslessBinary {
    fn frame(&self, bytes: &[u8], outgoing: &mut Vec<u8>) -> Result<(), Error> {
        if bytes.len() > (std::u16::MAX as usize) + 1 {
            return Err(anyhow::format_err!(
                "Packet length ({}) too long for stream framing",
                bytes.len()
            ));
        }
        outgoing.write_u16::<byteorder::LittleEndian>((bytes.len() - 1) as u16)?;
        outgoing.extend_from_slice(bytes);
        Ok(())
    }

    fn deframe(&self, bytes: &[u8]) -> Result<Deframed, Error> {
        if bytes.len() <= 3 {
            return Ok(Deframed { frame: None, unframed_bytes: 0, new_start_pos: 0 });
        }
        let len = 1 + (u16::from_le_bytes([bytes[0], bytes[1]]) as usize);
        if bytes.len() < 2 + len {
            // Not enough bytes to deframe: done for now.
            return Ok(Deframed { frame: None, unframed_bytes: 0, new_start_pos: 0 });
        }
        let frame = &bytes[2..2 + len];
        return Ok(Deframed {
            frame: Some(frame.to_vec()),
            unframed_bytes: 0,
            new_start_pos: 2 + len,
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::anyhow;
    use fuchsia_async::{Task, TimeoutExt};
    use onet_test_util::{test_security_context, LosslessPipe};
    use overnet_core::{NodeId, RouterOptions};
    use std::sync::Arc;
    use std::time::Duration;

    async fn await_peer(router: Arc<Router>, peer: NodeId) {
        let lp = router.new_list_peers_context();
        while lp.list_peers().await.unwrap().into_iter().find(|p| peer == p.id.into()).is_none() {}
    }

    #[test]
    fn simple_frame() {
        let framer = Framer::new(LosslessBinary);
        let body = vec![1, 2, 3, 4];
        let msg = framer.write_frame(&body).unwrap();

        let mut deframer = Deframer::new(LosslessBinary);
        assert_eq!(
            deframer.parse_frames(&msg).map(|frame| frame.unwrap()).collect::<Vec<_>>(),
            vec![ReadBytes::Framed(body)]
        );

        let body = vec![5, 6, 7, 8];
        let msg = framer.write_frame(&body).unwrap();
        assert_eq!(
            deframer.parse_frames(&msg).map(|frame| frame.unwrap()).collect::<Vec<_>>(),
            vec![ReadBytes::Framed(body)]
        );
    }

    #[test]
    fn framer_rejects_large_frame() {
        let big_slice = vec![0u8; 100000];
        let framer = Framer::new(LosslessBinary);
        assert!(framer.write_frame(&big_slice).is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_streamlink() {
        let rtr_client =
            Router::new(RouterOptions::new().set_node_id(1.into()), test_security_context())
                .unwrap();
        let rtr_server =
            Router::new(RouterOptions::new().set_node_id(2.into()), test_security_context())
                .unwrap();

        let (mut c2s_rx, mut c2s_tx) = LosslessPipe::new().split();
        let (mut s2c_rx, mut s2c_tx) = LosslessPipe::new().split();

        let rtr_client_fut = rtr_client.clone();
        let rtr_server_fut = rtr_server.clone();

        let _fwd = Task::spawn(
            futures::future::join(
                async move {
                    let run_client = run_stream_link(
                        rtr_client_fut,
                        &mut c2s_rx,
                        &mut s2c_tx,
                        LinkIntroductionFacts { you_are: None },
                        Box::new(|| None),
                    );

                    panic!("should never terminate: {:?}", run_client.await);
                },
                async move {
                    let run_server = run_stream_link(
                        rtr_server_fut,
                        &mut s2c_rx,
                        &mut c2s_tx,
                        LinkIntroductionFacts { you_are: None },
                        Box::new(|| None),
                    );

                    panic!("should never terminate: {:?}", run_server.await);
                },
            )
            .map(drop),
        );

        futures::future::join(
            await_peer(rtr_client.clone(), rtr_server.node_id()),
            await_peer(rtr_server.clone(), rtr_client.node_id()),
        )
        .map(Ok)
        .on_timeout(Duration::from_secs(120), || Err(anyhow!("timeout")))
        .await
        .unwrap();
    }
}
