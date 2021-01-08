// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::logs::{buffer::AccountedBuffer, message::Message};
use futures::{channel::mpsc::Receiver, StreamExt};
use parking_lot::Mutex;
use std::sync::Weak;

/// Listens to notifications on `on_new_messages` which indicate that log caches should be trimmed,
/// and ensures that the buffers are at or below the specified overall capacity.
// TODO(fxbug.dev/47661) take a weak DataRepoState and iterate over all log buffers
pub async fn keep_logs_trimmed(
    buffer: Weak<Mutex<AccountedBuffer<Message>>>,
    capacity: usize,
    mut on_new_messages: Receiver<()>,
) {
    // TODO(fxbug.dev/67210) drain the channel fully each time we are going to trim
    while let Some(()) = on_new_messages.next().await {
        if let Some(buffer) = buffer.upgrade() {
            buffer.lock().trim_to(capacity);
        } else {
            // the buffer is gone, no more work to do here
            return;
        }
    }
}
