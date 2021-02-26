// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    core::{
        pin::Pin,
        task::{Context, Poll},
    },
    fuchsia_bluetooth::types::Channel,
    futures::{
        ready,
        stream::{FusedStream, Stream, StreamExt},
    },
};

use crate::at::{AtHfMessage, Parser};

/// A connection between two peers that shares synchronized state and acts as the control plane for
/// HFP. See HFP v1.8, 4.2 for more information.
pub struct ServiceLevelConnection {
    /// The underlying RFCOMM channel connecting the peers.
    channel: Option<Channel>,
    /// An AT Command parser instance.
    parser: Parser,
}

impl ServiceLevelConnection {
    /// Returns `true` if an active connection exists between the peers.
    pub fn connected(&self) -> bool {
        self.channel.as_ref().map(|ch| !ch.is_terminated()).unwrap_or(false)
    }

    /// Connect using the provided `channel`.
    pub fn connect(&mut self, channel: Channel) {
        self.channel = Some(channel);
    }

    /// Create a new, unconnected `ServiceLevelConnection`.
    pub fn new() -> Self {
        Self { channel: None, parser: Parser::default() }
    }

    /// Consume bytes from the peer, producing a parsed AtHfMessage from the bytes.
    pub fn receive_data(&self, bytes: Vec<u8>) -> AtHfMessage {
        let command = self.parser.parse(&bytes);
        log::info!("Received {:?}", command);
        command
    }
}

impl Stream for ServiceLevelConnection {
    type Item = Result<AtHfMessage, fuchsia_zircon::Status>;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.is_terminated() {
            panic!("Cannot poll a terminated stream");
        }
        if let Some(channel) = &mut self.channel {
            Poll::Ready(
                ready!(channel.poll_next_unpin(cx))
                    .map(|item| item.map(|data| self.receive_data(data))),
            )
        } else {
            Poll::Pending
        }
    }
}

impl FusedStream for ServiceLevelConnection {
    fn is_terminated(&self) -> bool {
        !self.connected()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::protocol::features::HfFeatures, fuchsia_async as fasync,
        fuchsia_bluetooth::types::Channel, futures::io::AsyncWriteExt,
    };

    #[fasync::run_until_stalled(test)]
    async fn connected_state_before_and_after_connect() {
        let mut slc = ServiceLevelConnection::new();
        assert!(!slc.connected());
        let (_left, right) = Channel::create();
        slc.connect(right);
        assert!(slc.connected());
    }

    #[fasync::run_until_stalled(test)]
    async fn scl_stream_produces_items() {
        let mut slc = ServiceLevelConnection::new();
        let (mut left, right) = Channel::create();
        slc.connect(right);

        left.write_all(b"AT+BRSF=0\r").await.unwrap();

        let expected = AtHfMessage::HfFeatures(HfFeatures::from_bits(0).unwrap());

        let actual = match slc.next().await {
            Some(Ok(item)) => item,
            x => panic!("Unexpected stream item: {:?}", x),
        };
        assert_eq!(actual, expected);
    }

    #[fasync::run_until_stalled(test)]
    async fn scl_stream_terminated() {
        let mut slc = ServiceLevelConnection::new();
        let (left, right) = Channel::create();
        slc.connect(right);
        drop(left);

        assert_eq!(slc.next().await, None);
        assert!(!slc.connected());
        assert!(slc.is_terminated());
    }
}
