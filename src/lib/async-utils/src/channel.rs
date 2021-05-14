// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Additional functionality for use with asynchronous channels (futures::channel::mpsc).

use {
    core::{
        pin::Pin,
        task::{Context, Poll},
    },
    futures::{channel::mpsc, ready, Future},
};

/// Extends the functionality of a channel to include `try_send_fut`.
pub trait TrySend<Item> {
    /// Returns a future that will complete successfully when the item has been buffered in the
    /// channel or unsuccessfully when the receiving end has been dropped.
    ///
    /// The item is returned to the sender if it could not be sent. This is distinct from the
    /// functionality found in the `futures` library which will consume the item regardless of
    /// whether the item was successfully sent.
    ///
    /// NOTE: even in the event of successful completion, there is no guarantee that the receiver
    /// has consumed the message. It is possible for the receiver to drop its end of the channel
    /// without consuming all queued items.
    fn try_send_fut(&mut self, item: Item) -> TrySendFut<'_, Item>;
}

impl<Item> TrySend<Item> for mpsc::Sender<Item> {
    fn try_send_fut(&mut self, item: Item) -> TrySendFut<'_, Item> {
        TrySendFut::new(item, self)
    }
}

/// A Future that represents an ongoing send over a channel.
/// It completes when the send is complete or the send failed due to channel closure.
#[must_use]
pub struct TrySendFut<'a, Item> {
    // item must always be constructed with Some value to prevent a panic.
    item: Option<Item>,
    channel: &'a mut mpsc::Sender<Item>,
}

/// Returns a future that will complete successfully when the PeerTask has received the
/// message or unsuccessfully when the PeerTask has dropped its receiver.
impl<'a, Item> TrySendFut<'a, Item> {
    /// Construct a new `TrySendFut`.
    fn new(item: Item, channel: &'a mut mpsc::Sender<Item>) -> Self {
        Self { item: Some(item), channel }
    }
}

impl<'a, Item> Unpin for TrySendFut<'a, Item> {}

impl<'a, Item> Future for TrySendFut<'a, Item> {
    type Output = Result<(), Item>;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let recv_dropped = ready!(self.channel.poll_ready(cx)).is_err();
        let item = self.item.take().expect("Cannot poll without Some item");
        if recv_dropped {
            Poll::Ready(Err(item))
        } else {
            Poll::Ready(self.channel.try_send(item).map_err(|e| e.into_inner()))
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{TrySend, *},
        core::task::Poll,
        fuchsia_async as fasync,
        futures::{future::join, StreamExt},
    };

    #[fasync::run_until_stalled(test)]
    async fn item_future_completes_on_receive() {
        // Vec is chosen as the item type because it is !Copy and the primary use of this
        // method is for items that are not implicitly copiable.
        let (mut sender, mut receiver) = mpsc::channel(0);
        let (send_result, receive_result) =
            join(sender.try_send_fut(vec![1]), receiver.next()).await;
        assert_eq!(send_result, Ok(()));
        assert_eq!(receive_result, Some(vec![1]));
    }

    #[fasync::run_until_stalled(test)]
    async fn item_future_errors_on_receiver_closed() {
        let (mut sender, receiver) = mpsc::channel(0);
        // Drop receiving end to force an error.
        drop(receiver);
        let send_result = sender.try_send_fut(vec![1]).await;
        assert_eq!(send_result, Err(vec![1]));
    }

    #[test]
    fn item_future_pending_on_buffer_full() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (mut sender, mut receiver) = mpsc::channel(0);

        let send_result = exec.run_singlethreaded(sender.try_send_fut(vec![1]));
        assert_eq!(send_result, Ok(()));

        // Send a second item while the first is still in the channel.
        let mut send_fut = sender.try_send_fut(vec![2]);
        let send_poll_result = exec.run_until_stalled(&mut send_fut);
        assert_eq!(send_poll_result, Poll::Pending);

        // Consume the first item;
        let receive_poll_result = exec.run_until_stalled(&mut receiver.next());
        assert_eq!(receive_poll_result, Poll::Ready(Some(vec![1])));

        // Now there is room in the channel for the second item
        let send_poll_result = exec.run_until_stalled(&mut send_fut);
        assert_eq!(send_poll_result, Poll::Ready(Ok(())));

        // The second item is received
        let receive_poll_result = exec.run_until_stalled(&mut receiver.next());
        assert_eq!(receive_poll_result, Poll::Ready(Some(vec![2])));
    }
}
