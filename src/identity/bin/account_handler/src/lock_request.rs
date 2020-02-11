// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for handling lock requests.

use futures::channel::oneshot;
use futures::lock::Mutex;

/// A wrapper for oneshot::channel::<()>() for the interactions between
/// AccountHandler and Account during Account locking.
pub fn channel() -> (Sender, oneshot::Receiver<()>) {
    let (sender, receiver) = oneshot::channel();
    (Sender::Supported(Mutex::new(Some(sender))), receiver)
}

/// A wrapper around a oneshot::Sender<()>, with:
/// - non-mut send (using interior mutability)
/// - ability to be "unsupported", which fails with Error::NotSupported upon send
/// - fails with SendError::AlreadySent if already sent
pub enum Sender {
    /// Not supported, send will fail with SendError::NotSupported
    NotSupported,

    /// A sender wrapped in:
    /// - A mutex, for interior mutability.
    /// - An option, which becomes None when the sender is consumed.
    Supported(Mutex<Option<oneshot::Sender<()>>>),
}

#[derive(Debug, PartialEq)]
pub enum SendError {
    /// Lock requests are not supported
    NotSupported,

    /// A lock request has already been sent
    AlreadySent,

    /// The receiver of the lock request is no longer listening
    UnattendedReceiver,
}

impl Sender {
    /// Trigger the lock request.
    pub async fn send(&self) -> Result<(), SendError> {
        match self {
            Self::NotSupported => Err(SendError::NotSupported),
            Self::Supported(mutex) => {
                let mut sender_lock = mutex.lock().await;
                match sender_lock.take() {
                    Some(sender) => sender.send(()).map_err(|()| SendError::UnattendedReceiver),
                    None => Err(SendError::AlreadySent),
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_until_stalled(test)]
    async fn send_basic() {
        let (sender, mut receiver) = channel();
        assert_eq!(receiver.try_recv(), Ok(None));
        assert!(sender.send().await.is_ok());
        assert_eq!(receiver.await, Ok(()));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn send_then_drop() {
        // Tests a common data race where sender is dropped just after sending
        let (sender, receiver) = channel();
        sender.send().await.unwrap();
        std::mem::drop(sender);
        assert_eq!(receiver.await, Ok(()));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn send_already_sent_error() {
        let (sender, receiver) = channel();
        assert!(sender.send().await.is_ok());
        assert_eq!(receiver.await, Ok(()));
        assert_eq!(sender.send().await, Err(SendError::AlreadySent));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn send_unattended_receiver_error() {
        let (sender, receiver) = channel();
        std::mem::drop(receiver);
        assert_eq!(sender.send().await, Err(SendError::UnattendedReceiver));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn send_not_supported_error() {
        let sender = Sender::NotSupported;
        assert_eq!(sender.send().await, Err(SendError::NotSupported));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn receive_canceled_error() {
        let (sender, receiver) = channel();
        std::mem::drop(sender);
        assert_eq!(receiver.await, Err(oneshot::Canceled));
    }
}
