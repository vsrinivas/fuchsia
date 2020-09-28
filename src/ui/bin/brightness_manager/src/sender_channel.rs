// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::channel::mpsc::UnboundedSender;
use parking_lot::RwLock;
use std::sync::Arc;

/// Class for storing a list of senders and certain operations on them.
/// add_sender_channel adds a sender to the list.
/// send_value sends the new_value to the receivers.
/// If there is a new channel, the sender will be added to the list in the class.
/// Whenever value is changed, every sender in the list should send the new value to the sender.
/// If the channel is closed, remove corresponding sender from the list.
pub struct SenderChannel<T> {
    pub sender_channel_vec: Arc<RwLock<Vec<UnboundedSender<T>>>>,
}

impl<T> SenderChannel<T>
where
    T: Copy,
{
    pub fn new() -> SenderChannel<T> {
        SenderChannel { sender_channel_vec: Arc::new(RwLock::new(Vec::new())) }
    }

    pub async fn add_sender_channel(&mut self, sender: UnboundedSender<T>) {
        self.sender_channel_vec.write().push(sender);
    }

    pub fn send_value(&mut self, new_value: T) {
        let mut sender_channel_vec = self.sender_channel_vec.write();
        sender_channel_vec.retain(|sender| {
            let sender_success = sender.unbounded_send(new_value);
            match sender_success {
                Ok(_v) => true,
                Err(_e) => false,
            }
        })
    }
}

#[cfg(test)]

mod tests {
    use super::*;

    use fuchsia_async as fasync;

    fn mock_sender_channel() -> SenderChannel<f64> {
        SenderChannel::new()
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add_sender_channel() {
        let (channel_sender1, _channel_receiver1) = futures::channel::mpsc::unbounded::<f64>();
        let (channel_sender2, _channel_receiver2) = futures::channel::mpsc::unbounded::<f64>();
        let mut mock_sender_channel = mock_sender_channel();
        mock_sender_channel.add_sender_channel(channel_sender1).await;
        assert_eq!(mock_sender_channel.sender_channel_vec.write().len(), 1);
        mock_sender_channel.add_sender_channel(channel_sender2).await;
        assert_eq!(mock_sender_channel.sender_channel_vec.write().len(), 2);
    }
}
