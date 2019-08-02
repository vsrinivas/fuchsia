// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Watchers handles a list of watcher connections attached to a directory.  Watchers as described
//! in io.fidl.

use crate::{directory::connection::DirectoryEntryContainer, execution_scope::ExecutionScope};

use {fidl_fuchsia_io::MAX_FILENAME, fuchsia_async::Channel, slab::Slab, std::sync::Arc};

/// Wraps all watcher connections observing one directory.  The directory is responsible for
/// calling [`add`] and [`send_event`]/[`send_events`] methods when appropriate to make sure
/// watchers are observing a consistent view.
pub struct Watchers(Slab<watcher::Controller>);

impl Watchers {
    /// Constructs a new Watchers instance with no connected watchers.
    pub fn new() -> Self {
        Watchers(Slab::new())
    }

    /// Connects a new watcher (connected over the `channel`) to the list of watchers.  This
    /// watcher will receive the WATCH_EVENT_EXISTING event with all the names provided by the
    /// `names` argument.  `mask` is the event mask this watcher has requested.
    pub fn add<TraversalPosition>(
        &mut self,
        scope: ExecutionScope,
        directory: Arc<dyn DirectoryEntryContainer<TraversalPosition>>,
        names: Vec<String>,
        mask: u32,
        channel: Channel,
    ) where
        TraversalPosition: Default + Send + Sync + 'static,
    {
        let entry = self.0.vacant_entry();
        let key = entry.key();

        let done = move || {
            directory.unregister_watcher(key);
        };

        let controller = match watcher::new(scope, mask, channel, done) {
            Ok(controller) => entry.insert(controller),
            Err(_err) => {
                // If we failed to add a watcher, it should only happen due to the executor been
                // shutdown.  Nothing we can do here.  Slab will not add an entry, unless we call
                // `entry.insert()`.
                return;
            }
        };

        controller.send_events_existing(names);
        controller.send_event_idle();
    }

    /// Informs all the connected watchers about the specified event.  While `mask` and `event`
    /// carry the same information, as they are represented by `WATCH_MASK_*` and `WATCH_EVENT_*`
    /// constants in io.fidl, it is easier when both forms are provided.  `mask` is used to filter
    /// out those watchers that did not request for observation of this event and `event` is used
    /// to construct the event object.  The method will operate correctly only if `mask` and
    /// `event` match.
    ///
    /// In case of a communication error with any of the watchers, connection to this watcher is
    /// closed.
    pub fn send_event(&mut self, mask: u32, event: u8, name: String) {
        self.send_events(mask, event, vec![name])
    }

    /// Informs all the connected watchers about the specified event.  While `mask` and `event`
    /// carry the same information, as they are represented by `WATCH_MASK_*` and `WATCH_EVENT_*`
    /// constants in io.fidl, it is easier when both forms are provided.  `mask` is used to filter
    /// out those watchers that did not request for observation of this event and `event` is used
    /// to construct the event object.  The method will operate correctly only if `mask` and
    /// `event` match.
    ///
    /// In case of a communication error with any of the watchers, connection to this watcher is
    /// closed.
    pub fn send_events(&mut self, mask: u32, event: u8, names: Vec<String>) {
        let mut producer = WatcherEventsProducer::new(event, names);

        while producer.prepare_for_next_buffer() {
            let mut consumed_any = false;

            for (_key, controller) in self.0.iter() {
                controller.send_buffer_for(mask, || {
                    consumed_any = true;
                    producer.buffer()
                });
            }

            if !consumed_any {
                break;
            }
        }
    }

    /// Disconnects a watcher with the specified key.  A directory will use this method during the
    /// `unregister_watcher` call.
    pub fn remove(&mut self, key: usize) {
        self.0.remove(key);
    }
}

mod watcher {
    use crate::execution_scope::{ExecutionScope, SpawnError};

    use {
        fidl_fuchsia_io::{
            WATCH_EVENT_EXISTING, WATCH_EVENT_IDLE, WATCH_MASK_EXISTING, WATCH_MASK_IDLE,
        },
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

    use super::WatcherEventsProducer;

    /// `done` is not guaranteed to be called if the task failed to start.  It should only happen
    /// in case the return value is an `Err`.  Unfortunately, there is no way to return the `done`
    /// object itself, as the [`futures::Spawn::spawn_obj`] does not return the ownership in case
    /// of a failure.
    pub(crate) fn new(
        scope: ExecutionScope,
        mask: u32,
        channel: Channel,
        done: impl FnOnce() + Send + 'static,
    ) -> Result<Controller, SpawnError> {
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

        scope
            .spawn(Box::pin(FutureWithDrop::new(task, done)))
            .map(|()| Controller { mask, commands: sender })
    }

    pub(crate) struct Controller {
        mask: u32,
        commands: UnboundedSender<Command>,
    }

    impl Controller {
        pub(crate) fn send_buffer_for(&self, event_as_mask: u32, buffer: impl FnOnce() -> Vec<u8>) {
            if event_as_mask & self.mask == 0 {
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

        pub(crate) fn send_events_existing(&mut self, names: Vec<String>) {
            self.send_event_check_mask(WATCH_MASK_EXISTING, WATCH_EVENT_EXISTING, names);
        }

        pub(crate) fn send_event_idle(&mut self) {
            self.send_event_check_mask(WATCH_MASK_IDLE, WATCH_EVENT_IDLE, vec!["".to_string()]);
        }

        pub(crate) fn send_event_check_mask(&mut self, mask: u32, event: u8, names: Vec<String>) {
            if self.mask & mask == 0 {
                return;
            }

            let mut producer = WatcherEventsProducer::new(event, names);

            while producer.prepare_for_next_buffer() {
                let mut consumed_any = false;

                self.send_buffer_for(mask, || {
                    consumed_any = true;
                    producer.buffer()
                });

                if !consumed_any {
                    break;
                }
            }
        }
    }

    enum Command {
        Send(Vec<u8>),
    }

    fn handle_send(channel: &Channel, buffer: Vec<u8>) -> bool {
        channel.write(&*buffer, &mut vec![]).is_ok()
    }

    struct FutureWithDrop<Wrapped>
    where
        Wrapped: Future<Output = ()>,
    {
        task: Wrapped,
        done: Option<Box<dyn FnOnce() + Send>>,
    }

    impl<Wrapped> FutureWithDrop<Wrapped>
    where
        Wrapped: Future<Output = ()>,
    {
        // unsafe: `Self::drop` does not move the `task` value.  `Self` also does not implement
        // `Unpin`.  `task` is not `#[repr(packed)]`.
        unsafe_pinned!(task: Wrapped);

        fn new<Done>(task: Wrapped, done: Done) -> Self
        where
            Done: FnOnce() + Send + 'static,
        {
            Self { task, done: Some(Box::new(done)) }
        }
    }

    impl<Wrapped> Future for FutureWithDrop<Wrapped>
    where
        Wrapped: Future<Output = ()>,
    {
        type Output = ();

        fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
            self.as_mut().task().poll_unpin(cx)
        }
    }

    impl<Wrapped> Drop for FutureWithDrop<Wrapped>
    where
        Wrapped: Future<Output = ()>,
    {
        fn drop(&mut self) {
            match self.done.take() {
                Some(done) => done(),
                None => debug_assert!(false, "FutureWithDrop was destroyed twice?"),
            }
        }
    }
}

struct WatcherEventsProducer {
    event: u8,
    current_buffer: Option<Vec<u8>>,
    names: Vec<String>,
    next: usize,
}

impl WatcherEventsProducer {
    /// Constructs a new watcher events producer.  It will construct buffers with encoded
    /// representation of the event and the names of the entries.  Buffers can be obtained by
    /// calling [`prepare_for_next_buffer`], [`buffer`].
    fn new(event: u8, names: Vec<String>) -> Self {
        assert!(!names.is_empty());
        Self { event, current_buffer: None, names, next: 0 }
    }

    /// Checks if this producer can create another buffer, returning `true` if it can.  This method
    /// does not actually construct the buffer just yet, as an optimization if it will not be
    /// needed.
    fn prepare_for_next_buffer(&mut self) -> bool {
        self.current_buffer = None;
        self.next < self.names.len()
    }

    /// Returns a copy of the current buffer prepared by this producer.  This method will actually
    /// construct the buffer when called for the first time after a [`prepare_for_next_buffer`]
    /// invocation (or if the producer has been newly constructed).  In order to go to the next
    /// buffer one needs to call [`prepare_for_next_buffer`] explicitly.
    fn buffer(&mut self) -> Vec<u8> {
        match &self.current_buffer {
            Some(buf) => buf.clone(),
            None => {
                let buf = self.fill_buffer();
                self.current_buffer = Some(buf.clone());
                buf
            }
        }
    }

    fn fill_buffer(&mut self) -> Vec<u8> {
        let mut buffer = vec![];

        fn encode_name(buffer: &mut Vec<u8>, event: u8, name: &str) -> bool {
            if buffer.len() + (2 + name.len()) > fidl_fuchsia_io::MAX_BUF as usize {
                return false;
            }

            // We are going to encode the file name length as u8.
            debug_assert!(u8::max_value() as u64 >= MAX_FILENAME);

            buffer.push(event);
            buffer.push(name.len() as u8);
            buffer.extend_from_slice(name.as_bytes());
            true
        }

        while self.next < self.names.len() {
            if !encode_name(&mut buffer, self.event, &self.names[self.next]) {
                break;
            }
            self.next = self.next + 1;
        }

        buffer
    }
}
