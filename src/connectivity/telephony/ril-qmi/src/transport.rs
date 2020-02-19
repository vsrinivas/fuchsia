// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::QmuxError;
use bytes::Buf;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use parking_lot::Mutex;
use slab::Slab;
use std::collections::HashMap;
use std::future::Future;
use std::io::Cursor;
use std::marker::Unpin;
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll, Waker};

/// A client ID indicating the endpoint
#[derive(Default, Debug, Copy, Clone, PartialEq, Eq, Hash)]
pub struct ClientId(pub u8);

/// A service id for the QMI service
#[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
pub struct SvcId(pub u8);

/// A message interest id.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct TxId(pub u16);

impl TxId {
    fn as_raw_id(&self) -> usize {
        self.0 as usize
    }
}

/// A future which polls for the response to a client message.
#[must_use]
#[derive(Debug)]
pub struct QmiResponse {
    pub client_id: ClientId,
    pub svc_id: SvcId,
    pub tx_id: TxId,
    // `None` if the message response has been received
    pub transport: Option<Arc<QmiTransport>>,
}

impl Unpin for QmiResponse {}

impl Future for QmiResponse {
    type Output = Result<zx::MessageBuf, QmuxError>;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        let transport = this.transport.as_ref().ok_or(QmuxError::PollAfterCompletion)?;
        transport.poll_recv_msg_response(this.client_id, this.svc_id, this.tx_id, cx)
    }
}

impl Drop for QmiResponse {
    fn drop(&mut self) {
        if let Some(transport) = &self.transport {
            transport.deregister_interest(self.svc_id, self.client_id, self.tx_id);
            transport.wake_any();
        }
    }
}

/// An enum representing either a resolved message interest or a task on which to alert
/// that a response message has arrived.
#[derive(Debug)]
enum MessageInterest {
    /// A new `MessageInterest`
    WillPoll,
    /// A task is waiting to receive a response, and can be awoken with `Waker`.
    Waiting(Waker),
    /// A message has been received, and a task will poll to receive it.
    Received(zx::MessageBuf),
    /// A message has not been received, but the person interested in the response
    /// no longer cares about it, so the message should be discarded upon arrival.
    Discard,
}

impl MessageInterest {
    /// Check if a message has been received.
    fn is_received(&self) -> bool {
        if let MessageInterest::Received(_) = *self {
            return true;
        }
        false
    }

    fn unwrap_received(self) -> zx::MessageBuf {
        if let MessageInterest::Received(buf) = self {
            return buf;
        }
        panic!("EXPECTED received message")
    }
}

#[derive(Debug)]
pub struct QmuxHeader {
    pub length: u16,
    pub ctrl_flags: u8,
    pub svc_type: u8,
    pub client_id: u8,
    // general service header
    pub svc_ctrl_flags: u8,
    pub transaction_id: u16, // TODO this needs to be u16 for anything not a CTL
}

pub fn parse_qmux_header<T: Buf>(buf: &mut T) -> Result<QmuxHeader, QmuxError> {
    // QMUX headers start with 0x01
    if 0x01 != buf.get_u8() {
        return Err(QmuxError::Invalid);
    }
    let length = buf.get_u16_le();
    let ctrl_flags = buf.get_u8();
    let svc_type = buf.get_u8();
    let client_id = buf.get_u8();
    let svc_ctrl_flags;
    let transaction_id;
    // TODO(bwb): Consider passing these parameters in from the Decodable trait'd object,
    // more generic than a hardcode for CTL interfaces
    if svc_type == 0x00 {
        svc_ctrl_flags = buf.get_u8();
        // ctl service is one byte
        transaction_id = buf.get_u8() as u16;
    } else {
        svc_ctrl_flags = buf.get_u8() >> 1;
        transaction_id = buf.get_u16_le();
        // The bits for the ctrl flags are shifted by one in non CTL messages
    }
    Ok(QmuxHeader { length, ctrl_flags, svc_type, client_id, svc_ctrl_flags, transaction_id })
}

/// Shared transport channel
#[derive(Debug)]
pub struct QmiTransport {
    pub transport_channel: Option<fasync::Channel>,
    message_interests: Mutex<HashMap<(SvcId, ClientId), Slab<MessageInterest>>>,
}

impl QmiTransport {
    pub fn new(chan: fasync::Channel) -> Self {
        QmiTransport {
            transport_channel: Some(chan),
            message_interests: Mutex::new(HashMap::new()),
        }
    }

    pub fn register_interest(&self, svc_id: SvcId, client_id: ClientId) -> TxId {
        let mut lock = self.message_interests.lock();
        let interests = lock.entry((svc_id, client_id)).or_insert(Slab::<MessageInterest>::new());
        let id = interests.insert(MessageInterest::WillPoll);
        TxId(id as u16)
    }

    pub fn deregister_interest(&self, svc_id: SvcId, client_id: ClientId, tx_id: TxId) {
        let mut lock = self.message_interests.lock();
        let id = tx_id.as_raw_id();
        if let Some(ref mut interests) = lock.get_mut(&(svc_id, client_id)) {
            if interests.contains(id) {
                if interests[id].is_received() {
                    interests.remove(id as usize);
                } else {
                    interests[id] = MessageInterest::Discard;
                }
            }
        }
    }

    // Wakes up an arbitrary task that has begun polling on the channel so that
    // it will call recv_all and be registered as the new channel reader.
    fn wake_any(&self) {
        let lock = self.message_interests.lock();
        // any service/client will do
        for (_, message_interest_map) in lock.iter() {
            // any TxId will do
            for (_, message_interest) in message_interest_map.iter() {
                if let MessageInterest::Waiting(waker) = message_interest {
                    waker.wake_by_ref();
                    return;
                }
            }
        }
        // TODO use client code from fidl for event/indication inspiration
    }

    /// Poll for the receipt of any response message or an event.
    ///
    /// Returns whether or not the channel is closed.
    fn recv_all(&self, cx: &mut Context<'_>) -> Result<bool, QmuxError> {
        if let Some(ref transport_channel) = self.transport_channel {
            if transport_channel.is_closed() {
                return Ok(true);
            }
            loop {
                let mut buf = zx::MessageBuf::new();
                match transport_channel.recv_from(cx, &mut buf) {
                    Poll::Ready(Ok(())) => {}
                    Poll::Ready(Err(zx::Status::PEER_CLOSED)) => return Ok(true),
                    Poll::Ready(Err(e)) => return Err(QmuxError::ClientRead(e)),
                    Poll::Pending => return Ok(false),
                }
                let mut buf = Cursor::new(buf.bytes());
                let header = parse_qmux_header(&mut buf)?;

                // TODO add indication support here, only handles responses for now
                // This is a response for ONLY the CTL interface, will need indication support
                // just throw them away for now
                if header.svc_ctrl_flags != 0x01 {
                    continue;
                }

                let mut mi = self.message_interests.lock();
                if let Some(ref mut interest_slab) =
                    mi.get_mut(&(SvcId(header.svc_type), ClientId(header.client_id)))
                {
                    let tx_id = TxId(header.transaction_id.into());
                    let raw_tx_id = tx_id.as_raw_id() - 1;
                    if let Some(&MessageInterest::Discard) = (*interest_slab).get(raw_tx_id) {
                        interest_slab.remove(raw_tx_id);
                    } else if let Some(entry) = interest_slab.get_mut(raw_tx_id) {
                        let dst: Vec<u8> = buf.bytes().to_vec();
                        let new_buf = zx::MessageBuf::new_with(dst, Vec::new());
                        let old_entry =
                            std::mem::replace(entry, MessageInterest::Received(new_buf));
                        if let MessageInterest::Waiting(waker) = old_entry {
                            waker.wake();
                        }
                    }
                }
            }
        } else {
            return Ok(false);
        }
    }

    pub fn poll_recv_msg_response(
        &self,
        client_id: ClientId,
        svc_id: SvcId,
        txid: TxId,
        cx: &mut Context<'_>,
    ) -> Poll<Result<zx::MessageBuf, QmuxError>> {
        let is_closed = self.recv_all(cx)?;
        let mut mi = self.message_interests.lock();
        let message_interests: &mut Slab<MessageInterest> =
            mi.get_mut(&(svc_id, client_id)).ok_or(QmuxError::InvalidSvcOrClient)?;
        if message_interests
            .get(txid.as_raw_id())
            .expect("Polled unregistered interest")
            .is_received()
        {
            let buf = message_interests.remove(txid.as_raw_id()).unwrap_received();
            Poll::Ready(Ok(buf))
        } else {
            // Set the current waker to be notified when a response arrives.
            *message_interests.get_mut(txid.as_raw_id()).expect("Polled unregistered interest") =
                MessageInterest::Waiting(cx.waker().clone());
            if is_closed {
                Poll::Ready(Err(QmuxError::ClientRead(zx::Status::PEER_CLOSED)))
            } else {
                Poll::Pending
            }
        }
    }
}
