// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A task that is run to process communication with an individual watcher.

use crate::{directory::watchers::event_producers::EventProducer, execution_scope::ExecutionScope};

use {
    fuchsia_async::Channel,
    fuchsia_zircon::MessageBuf,
    futures::{
        channel::mpsc::{self, UnboundedSender},
        select,
        stream::StreamExt,
        task::{Context, Poll},
        Future, FutureExt,
    },
    pin_utils::unsafe_pinned,
    std::{ops::Drop, pin::Pin},
};

/// `done` is not guaranteed to be called if the task failed to start.  It should only happen
/// in case the return value is an `Err`.  Unfortunately, there is no way to return the `done`
/// object itself, as the [`futures::Spawn::spawn_obj`] does not return the ownership in case
/// of a failure.
pub(crate) fn new(
    scope: ExecutionScope,
    mask: u32,
    channel: Channel,
    done: impl FnOnce() + Send + 'static,
) -> Controller {
    let (sender, mut receiver) = mpsc::unbounded();

    let task = async move {
        let mut buf = MessageBuf::new();
        let mut recv_msg = channel.recv_msg(&mut buf).fuse();
        loop {
            select! {
                command = receiver.next() => match command {
                    Some(Command::Send(buffer)) => {
                        let success = handle_send(&channel, buffer);
                        if !success {
                            break;
                        }
                    },
                    Some(Command::Disconnect) => break,
                    None => break,
                },
                _ = recv_msg => {
                    // We do not expect any messages to be received over the watcher connection.
                    // Should we receive a message we will close the connection to indicate an
                    // error.  If any error occurs, we also close the connection.  And if the
                    // connection is closed, we just stop the command processing as well.
                    break;
                },
            }
        }
    };

    scope.spawn(Box::pin(FutureWithDrop::new(task, done)));
    Controller { mask, commands: sender }
}

pub struct Controller {
    mask: u32,
    commands: UnboundedSender<Command>,
}

impl Controller {
    /// Sends a buffer to the connected watcher.  `mask` specifies the type of the event the buffer
    /// is for.  If the watcher mask does not include the event specified by the `mask` then the
    /// buffer is not sent and `buffer` is not even invoked.
    pub(crate) fn send_buffer(&mut self, mask: u32, buffer: impl FnOnce() -> Vec<u8>) {
        if self.mask & mask == 0 {
            return;
        }

        if self.commands.unbounded_send(Command::Send(buffer())).is_ok() {
            return;
        }

        // An error to send indicates the execution task has been disconnected.  Controller should
        // always be removed from the watchers list before it is destroyed.  So this is some
        // logical bug.
        debug_assert!(false, "Watcher controller failed to send a command to the watcher.");
    }

    /// Uses a `producer` to generate one or more buffers and send them all to the connected
    /// watcher.  `producer.mask()` is used to determine the type of the event - in case the
    /// watcher mask does not specify that it needs to receive this event, then the producer is not
    /// used and `false` is returned.  If the producers mask and the watcher mask overlap, then
    /// `true` is returned (even if the producer did not generate a single buffer).
    pub fn send_event(&mut self, producer: &mut dyn EventProducer) -> bool {
        if self.mask & producer.mask() == 0 {
            return false;
        }

        while producer.prepare_for_next_buffer() {
            let buffer = producer.buffer();
            if self.commands.unbounded_send(Command::Send(buffer)).is_ok() {
                continue;
            }

            // An error to send indicates the execution task has been disconnected.  Controller
            // should always be removed from the watchers list before it is destroyed.  So this is
            // some logical bug.
            debug_assert!(false, "Watcher controller failed to send a command to the watcher.");
        }

        return true;
    }

    /// Initiates disconnection between the watcher and this controller.  `disconnect` exits
    /// immediately, after sending a command that still need to be processed by the watcher task.
    /// It is the responsibility of the watcher task to remove the controller from the list of
    /// controllers in the [`EntryContainer`] this controller was added to.
    pub(crate) fn disconnect(&mut self) {
        if self.commands.unbounded_send(Command::Disconnect).is_ok() {
            return;
        }

        // An error to send indicates the execution task has been disconnected.  Controller should
        // always be removed from the watchers list before it is destroyed.  So this is some
        // logical bug.
        debug_assert!(false, "Watcher controller failed to send a command to the watcher.");
    }
}

enum Command {
    Send(Vec<u8>),
    Disconnect,
}

fn handle_send(channel: &Channel, buffer: Vec<u8>) -> bool {
    channel.write(&*buffer, &mut vec![]).is_ok()
}

struct FutureWithDrop<Wrapped, Done>
where
    Wrapped: Future<Output = ()>,
    Done: FnOnce() + Send + 'static,
{
    task: Wrapped,
    done: Option<Done>,
}

impl<Wrapped, Done> FutureWithDrop<Wrapped, Done>
where
    Wrapped: Future<Output = ()>,
    Done: FnOnce() + Send + 'static,
{
    // unsafe: `Self::drop` does not move the `task` value.  `Self` also does not implement
    // `Unpin`.  `task` is not `#[repr(packed)]`.
    unsafe_pinned!(task: Wrapped);

    fn new(task: Wrapped, done: Done) -> Self {
        Self { task, done: Some(done) }
    }
}

impl<Wrapped, Done> Future for FutureWithDrop<Wrapped, Done>
where
    Wrapped: Future<Output = ()>,
    Done: FnOnce() + Send + 'static,
{
    type Output = ();

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.as_mut().task().poll_unpin(cx)
    }
}

impl<Wrapped, Done> Drop for FutureWithDrop<Wrapped, Done>
where
    Wrapped: Future<Output = ()>,
    Done: FnOnce() + Send + 'static,
{
    fn drop(&mut self) {
        match self.done.take() {
            Some(done) => done(),
            None => debug_assert!(false, "FutureWithDrop was destroyed twice?"),
        }
    }
}
