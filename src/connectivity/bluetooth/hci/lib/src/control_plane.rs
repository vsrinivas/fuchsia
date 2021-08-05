// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
/// The control plane is used to send DDK lifecycle and bt-transport protocol messages to the
/// library's worker thread from the driver. `ControlPlane` is the struct which contains a channel
/// of communication with the worker thread, `Message`s indicate which operation should be
/// performed or which resources are being passed into the thread, and `Responder`s are objects
/// with which the worker thread can send back responses pertaining to the success or failure of
/// a requested operation.
use {
    core::{
        pin::Pin,
        task::{Context, Poll},
    },
    fuchsia_zircon::{self as zx, Duration},
    futures::{
        channel::mpsc::{channel, Receiver, Sender},
        Sink,
    },
    std::sync,
};

use crate::{log::*, transport::HwTransportBuilder};

/// A `ControlPlane` does not support sending multiple outstanding unacknowledged messages. Each
/// message must be acknowledged by the `Worker` before another message can be sent. The public API
/// ensures that this contract is kept. The ControlPlane's channel does not need to be larger than 1
/// because of this.
const CONTROL_PLANE_CHANNEL_SIZE: usize = 1;

/// Responder channels should have an available slot to store a message in so that the `Worker`
/// is not blocked on sending a response to the thread that sent a message using the `ControlPlane`.
const CONTROL_PLANE_RESPONDER_CHANNEL_SIZE: usize = 1;

/// A type that can be boxed up into a `Responder`. See `Responder` documentation for details on
/// the usage.
pub trait Respondable: Sink<zx::Status, Error = zx::Status> + Unpin + Send + 'static {}

/// Responders are paired with Messages and sent across threads via channels. They should be used
/// to respond to the Message that they are paired with.
///
/// Responders are always used from within an async context.
pub type Responder = Box<dyn Respondable>;

/// A `ControlPlane` is used to communicate with the internal library thread. The `ControlPlane`
/// manages waiting for a response, handling timeouts, propagating errors that occur when sending
/// messages, and can be closed. It can be used from either synchronous and asynchronous contexts,
/// by calling the send and async_send methods respectively.
pub struct ControlPlane {
    sender: Option<Sender<(Message, Responder)>>,
}

impl ControlPlane {
    /// Create a new `ControlPlane` and the paired recipient which is a `Receiver` instance.
    pub fn new() -> (ControlPlane, Receiver<(Message, Responder)>) {
        let (sender, receiver) = channel(CONTROL_PLANE_CHANNEL_SIZE);
        (ControlPlane { sender: Some(sender) }, receiver)
    }

    /// Send a message and wait for a response, giving up after `timeout`.
    /// This should be used in cases where a client with a `WorkerHandle` is operating in
    /// a blocking context. In general, that means via another language over an FFI boundary.
    pub fn send(&mut self, msg: Message, timeout: Duration) -> zx::Status {
        if let Some(sender) = &mut self.sender {
            // The communication between an ControlPlane and the recipient on the other end happens
            // in request/response pairs. First, create a response_sender and paired
            // reponse_receiver. The response_sender is sent with the message to the recipient to
            // which this ControlPlane is associated. The recipient will then respond using the
            // response_sender and the ControlPlane can receive that response over the
            // response_receiver channel.
            let (response_sender, response_receiver) = SyncResponder::new();
            if let Err(e) = sender.try_send((msg, response_sender.boxed())) {
                bt_log_err!("Could not communicate with worker thread: {:?}", e);
                return zx::Status::INTERNAL;
            };
            let timeout = core::time::Duration::from_nanos(timeout.into_nanos() as u64);
            response_receiver.recv_timeout(timeout).unwrap_or(zx::Status::TIMED_OUT)
        } else {
            zx::Status::INTERNAL
        }
    }

    /// Send a message, asynchronously waiting for a response. This should be used in cases where
    /// the client is operating in an asynchronous context in rust.
    // This is currently only needed for unittests.
    #[cfg(test)]
    pub async fn async_send(&mut self, msg: Message) -> zx::Status {
        if let Some(sender) = &mut self.sender {
            use futures::stream::StreamExt;
            // The communication between an ControlPlane and the recipient on the other end happens in
            // request/response pairs. First, create a response_sender and paired reponse_receiver.
            // The response_sender is sent with the message to the recipient to which this ControlPlane is
            // associated. The recipient will then respond using the response_sender and the ControlPlane
            // can receive that response over the response_receiver channel.
            let (response_sender, mut response_receiver) = AsyncResponder::new();
            if let Err(e) = sender.try_send((msg, response_sender.boxed())) {
                bt_log_err!("Could not communicate with worker thread: {:?}", e);
                return zx::Status::INTERNAL;
            };
            response_receiver.next().await.unwrap_or(zx::Status::TIMED_OUT)
        } else {
            zx::Status::INTERNAL
        }
    }

    /// Drop the send end of a channel, indicating to the other end that it is closed.
    /// Messages sent after `close` will return a `ZX_ERR_INTERNAL` status.
    /// Ther is no way to reopen an ControlPlane after it is closed and close calls made after
    /// an ControlPlane is closed will have no effect.
    pub fn close(&mut self) {
        self.sender = None;
    }
}

/// Represents the messages that can be sent to the rust thread.
pub enum Message {
    /// A resource representing the underlying transport has been opened and is contained within
    /// this message.
    #[allow(dead_code)] // removed in fxrev.dev/339230
    OpenTransport(Box<dyn HwTransportBuilder>),
    /// Contains one end of a channel representing the HCI Command channel.
    OpenCmd(zx::Channel),
    /// Contains one end of a channel representing the HCI ACL Data channel.
    OpenAcl(zx::Channel),
    /// Contains one end of a channel representing a snooper.
    OpenSnoop(zx::Channel),
    /// Notify the worker that it should perform any tasks required by an unbind invokation
    Unbind,
}

/// When a Message is sent from a non-async context, it must be paired with a `Responder` of this
/// concrete type.
struct SyncResponder(sync::mpsc::SyncSender<zx::Status>);

impl SyncResponder {
    pub fn new() -> (Self, sync::mpsc::Receiver<zx::Status>) {
        let (responder, response_receiver) =
            sync::mpsc::sync_channel(CONTROL_PLANE_RESPONDER_CHANNEL_SIZE);
        (SyncResponder(responder), response_receiver)
    }

    pub fn boxed(self) -> Box<Self> {
        Box::new(self)
    }
}

impl Sink<zx::Status> for SyncResponder {
    type Error = zx::Status;
    fn poll_ready(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(Ok(()))
    }

    fn start_send(self: Pin<&mut Self>, item: zx::Status) -> Result<(), Self::Error> {
        self.0.try_send(item).map_err(|_| zx::Status::INTERNAL)
    }

    fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(Ok(()))
    }

    fn poll_close(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(Ok(()))
    }
}

impl Unpin for SyncResponder {}
impl Respondable for SyncResponder {}

// This is currently only needed for unittests.
#[cfg(test)]
mod async_responder {
    use super::*;

    /// When a Message is sent from an async context, it must be paired with a `Responder` of this
    /// concrete type.
    pub(super) struct AsyncResponder(Sender<zx::Status>);

    impl AsyncResponder {
        pub fn new() -> (Self, Receiver<zx::Status>) {
            let (responder, response_receiver) = channel(CONTROL_PLANE_RESPONDER_CHANNEL_SIZE);
            (AsyncResponder(responder), response_receiver)
        }

        pub fn boxed(self) -> Box<Self> {
            Box::new(self)
        }
    }

    impl Sink<zx::Status> for AsyncResponder {
        type Error = zx::Status;
        fn poll_ready(
            mut self: Pin<&mut Self>,
            cx: &mut Context<'_>,
        ) -> Poll<Result<(), Self::Error>> {
            self.0.poll_ready(cx).map_err(|_| zx::Status::INTERNAL)
        }

        fn start_send(mut self: Pin<&mut Self>, item: zx::Status) -> Result<(), Self::Error> {
            self.0.start_send(item).map_err(|_| zx::Status::INTERNAL)
        }

        fn poll_flush(
            mut self: Pin<&mut Self>,
            cx: &mut Context<'_>,
        ) -> Poll<Result<(), Self::Error>> {
            Pin::new(&mut self.0).poll_flush(cx).map_err(|_| zx::Status::INTERNAL)
        }

        fn poll_close(
            mut self: Pin<&mut Self>,
            cx: &mut Context<'_>,
        ) -> Poll<Result<(), Self::Error>> {
            Pin::new(&mut self.0).poll_close(cx).map_err(|_| zx::Status::INTERNAL)
        }
    }

    impl Unpin for AsyncResponder {}
    impl Respondable for AsyncResponder {}
}

#[cfg(test)]
use async_responder::*;

#[cfg(test)]
mod tests {
    use super::*;
    use futures::{SinkExt, StreamExt};

    #[test]
    fn control_plane_send_success() {
        let (mut control_plane, mut receiver) = ControlPlane::new();

        let _ = std::thread::spawn(move || {
            let mut e = fuchsia_async::TestExecutor::new().unwrap();
            let fut = async {
                let (_, mut responder) = receiver.next().await.expect(
                    "ControlPlane dropped the sender end of the channel before sending a message",
                );
                responder
                    .send(zx::Status::OK)
                    .await
                    .expect("ControlPlane dropped receiver end of responder channel");
            };
            e.run_singlethreaded(fut);
        });

        let (tx, _) = zx::Channel::create().unwrap();
        // Send message will timeout in 30s if no response is received.
        let status = control_plane.send(Message::OpenCmd(tx), Duration::from_seconds(30));
        assert_eq!(zx::Status::OK, status);
    }

    #[test]
    fn control_plane_send_timeout() {
        let (mut control_plane, _) = ControlPlane::new();
        let (tx, _) = zx::Channel::create().unwrap();
        let status = control_plane.send(Message::OpenCmd(tx), Duration::from_millis(0));
        assert_eq!(zx::Status::INTERNAL, status);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn control_plane_async_send_success() {
        let (mut control_plane, mut receiver) = ControlPlane::new();
        let (tx, _) = zx::Channel::create().unwrap();

        let test = async { control_plane.async_send(Message::OpenCmd(tx)).await };

        let responder = async {
            let (_, mut responder) = receiver.next().await.expect(
                "ControlPlane dropped the sender end of the channel before sending a message",
            );
            responder
                .send(zx::Status::OK)
                .await
                .expect("ControlPlane dropped receiver end of responder channel");
        };

        let (status, _) = futures::future::join(test, responder).await;
        assert_eq!(zx::Status::OK, status);
    }
}
