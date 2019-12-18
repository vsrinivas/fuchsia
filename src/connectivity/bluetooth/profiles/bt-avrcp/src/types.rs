// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avctp::Error as AvctpError,
    failure::{Error, Fail},
    futures::{sink::Sink, stream::FusedStream, Stream},
    pin_utils::unsafe_pinned,
    std::{
        pin::Pin,
        task::{Context, Poll},
    },
};

use crate::packets::Error as PacketError;

// TODO(BT-2197): change to the BT shared peer id type when the BrEdr protocol changes over.
pub type PeerId = String;

/// The error types for peer management.
#[derive(Fail, Debug)]
pub enum PeerError {
    /// Error encoding/decoding packet
    #[fail(display = "Packet encoding/decoding error: {:?}", _0)]
    PacketError(PacketError),

    /// Error in protocol layer
    #[fail(display = "Protocol layer error: {:?}", _0)]
    #[allow(dead_code)]
    AvctpError(AvctpError),

    #[fail(display = "Remote device was not connected")]
    RemoteNotFound,

    #[fail(display = "Remote command is unsupported")]
    CommandNotSupported,

    #[fail(display = "Remote command rejected")]
    CommandFailed,

    #[fail(display = "Unable to connect")]
    ConnectionFailure(#[cause] Error),

    #[fail(display = "Unexpected response to command")]
    UnexpectedResponse,

    #[fail(display = "Generic errors")]
    GenericError(#[cause] Error),

    #[doc(hidden)]
    #[fail(display = "__Nonexhaustive error should never be created.")]
    __Nonexhaustive,
}

impl From<AvctpError> for PeerError {
    fn from(error: AvctpError) -> Self {
        PeerError::AvctpError(error)
    }
}

impl From<PacketError> for PeerError {
    fn from(error: PacketError) -> Self {
        PeerError::PacketError(error)
    }
}

impl From<Error> for PeerError {
    fn from(error: Error) -> Self {
        PeerError::GenericError(error)
    }
}

/// A specialized stream combinator similar to Map. PeerIdStreamMap encapsulates another stream and
/// wraps each item returned by the stream in a tuple that also returns the a specified PeerId as
/// the first field and the wrapped item response as the second field.
#[derive(Debug)]
#[must_use = "streams do nothing unless polled"]
pub struct PeerIdStreamMap<St> {
    stream: St,
    peer_id: PeerId,
}

impl<St: Unpin> Unpin for PeerIdStreamMap<St> {} // Conditional Unpin impl to make unsafe_pinned safe.

impl<St> PeerIdStreamMap<St>
where
    St: Stream,
{
    unsafe_pinned!(stream: St);

    pub fn new(stream: St, peer_id: &PeerId) -> PeerIdStreamMap<St> {
        Self { stream, peer_id: peer_id.clone() }
    }
}

impl<St: FusedStream> FusedStream for PeerIdStreamMap<St> {
    fn is_terminated(&self) -> bool {
        self.stream.is_terminated()
    }
}

impl<St> Stream for PeerIdStreamMap<St>
where
    St: Stream,
{
    type Item = (PeerId, St::Item);

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.as_mut().stream().poll_next(cx).map(|opt| opt.map(|t| (self.peer_id.clone(), t)))
    }
}

// Forwarding impl of Sink to the underlying stream if there is one.
impl<St: Stream + Sink<Item>, Item> Sink<Item> for PeerIdStreamMap<St> {
    type Error = St::Error;

    fn poll_ready(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        self.stream().poll_ready(cx)
    }

    fn start_send(self: Pin<&mut Self>, item: Item) -> Result<(), Self::Error> {
        self.stream().start_send(item)
    }

    fn poll_flush(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        self.stream().poll_flush(cx)
    }

    fn poll_close(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        self.stream().poll_close(cx)
    }
}
