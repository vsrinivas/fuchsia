// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! In order to process watcher events every lazy directory that supports watchers needs an
//! "active" component.  This module holds this active component, responsible for processing
//! watcher events for a single directory.

use super::{WatcherCommand, WatcherEvent};

use crate::{
    directory::{
        entry_container::Directory,
        traversal_position::TraversalPosition,
        watchers::{
            event_producers::{SingleNameEventProducer, StaticVecEventProducer},
            Watchers,
        },
    },
    execution_scope::ExecutionScope,
};

use {
    fidl_fuchsia_io::WATCH_MASK_EXISTING,
    fuchsia_async::Channel,
    futures::{
        channel::mpsc::UnboundedReceiver,
        select,
        stream::{Stream, StreamExt},
    },
    pin_utils::pin_mut,
    std::sync::Arc,
};

/// Runs a command loop for all watchers attached to a lazy directory, processing
/// [`WatcherCommand`] commands and any [`WatcherEvents`].
pub(super) async fn run<WatcherEvents>(
    directory: Arc<dyn Directory>,
    mut commands: UnboundedReceiver<WatcherCommand>,
    watcher_events: WatcherEvents,
) where
    WatcherEvents: Stream<Item = WatcherEvent> + Send + 'static,
{
    let mut watchers = Watchers::new();

    pin_mut!(watcher_events);
    let mut watcher_events = watcher_events.fuse();
    loop {
        select! {
            o_command = commands.next() => match o_command {
                Some(command) => match command {
                    WatcherCommand::RegisterWatcher { scope, mask, channel } => {
                        handle_register_watcher(
                                &mut watchers,
                                directory.clone(),
                                scope.clone(),
                                mask,
                                channel,
                        ).await;
                    },
                    WatcherCommand::UnregisterWatcher { key } => {
                        watchers.remove(key);
                    },
                },
                None => break,
            },
            o_event = watcher_events.next() => match o_event {
                Some(event) => match event {
                    WatcherEvent::Deleted(name) => {
                        watchers.send_event(&mut SingleNameEventProducer::deleted(&name));
                    },
                    WatcherEvent::Added(names) => {
                        watchers.send_event(&mut StaticVecEventProducer::added(names));
                    },
                    WatcherEvent::Removed(names) => {
                        watchers.send_event(&mut StaticVecEventProducer::removed(names));
                    },
                },
                None => break,
            },
        }
    }
}

async fn handle_register_watcher(
    watchers: &mut Watchers,
    directory: Arc<dyn Directory>,
    scope: ExecutionScope,
    mask: u32,
    channel: Channel,
) {
    // Optimize the case when we do not need to send the list of existing entries.
    if mask & WATCH_MASK_EXISTING == 0 {
        let controller = watchers.add(scope, directory, mask, channel);
        controller.send_event(&mut SingleNameEventProducer::idle());
        return;
    }

    let controller = watchers.add(scope, directory.clone(), mask, channel);

    // We will use `directory.read_dirents` to produce up to one full buffer of entry names, that
    // we will send to the new watcher.  `pos` will be used to remember the position in the listing
    // where we have stopped when we got a full buffer, in order to resume.  The reason we do it
    // this way is to allow `read_dirents` to be async, while the `send_event` method that is
    // consuming the event producer is not async.

    let mut pos = TraversalPosition::Start;
    loop {
        let directory = directory.clone();
        let res = directory.read_dirents(&pos, single_buffer::existing()).await;

        let sealed_or_err = match res {
            Ok((new_pos, sealed)) => {
                pos = new_pos;
                sealed.open().downcast::<single_buffer::Sealed>()
            }
            Err(_status) => {
                controller.disconnect();
                return;
            }
        };

        let sealed = match sealed_or_err {
            Ok(done) => done,
            Err(_) => {
                debug_assert!(
                    false,
                    "`read_dirents()` returned a `dirents_sink::Sealed` instance that is not an \
                     instance of the directory::lazy::watchers_task::single_buffer::Sealed`.  This
                     is a bug in the `read_dirents()` implementation."
                );
                break;
            }
        };

        match sealed.unpack() {
            Some(mut existing_event) => {
                controller.send_event(&mut existing_event);
            }
            None => break,
        }
    }

    controller.send_event(&mut SingleNameEventProducer::idle());
}

/// Event producer that is used with the lazy directory.
mod single_buffer {
    use crate::directory::{
        dirents_sink,
        entry::EntryInfo,
        watchers::event_producers::{encode_name, SingleBufferEventProducer},
    };

    use {fidl_fuchsia_io::WATCH_EVENT_EXISTING, std::any::Any};

    pub(super) fn existing() -> Box<Sink> {
        Box::new(Sink { buffer: vec![] })
    }

    /// This is a [`dirents_sink::Sink`] that will accept as many entries as would fit into one
    /// full buffer and then will report that the sink is full.
    /// [`BufferProducer`] is an [`EventProducer`] that is generated as a result.  It can then be
    /// used to send this buffer to all the watcher, and to get back a [`Sink`] that would continue
    /// the traversal.
    ///
    /// It only supports producing events of type WATCH_EVENT_EXISTING, as this is the only case
    /// where a lazy directory will use it.
    pub(super) struct Sink {
        buffer: Vec<u8>,
    }

    impl dirents_sink::Sink for Sink {
        fn append(
            mut self: Box<Self>,
            _entry: &EntryInfo,
            name: &str,
        ) -> dirents_sink::AppendResult {
            use dirents_sink::AppendResult;

            if encode_name(&mut self.buffer, WATCH_EVENT_EXISTING, name) {
                AppendResult::Ok(self)
            } else {
                AppendResult::Sealed(Sealed::new(self.buffer))
            }
        }

        fn seal(self: Box<Self>) -> Box<dyn dirents_sink::Sealed> {
            Sealed::new(self.buffer)
        }
    }

    pub(super) struct Sealed {
        buffer: Vec<u8>,
    }

    impl Sealed {
        fn new(buffer: Vec<u8>) -> Box<Self> {
            Box::new(Self { buffer })
        }

        pub(super) fn unpack(self) -> Option<SingleBufferEventProducer> {
            if self.buffer.is_empty() {
                None
            } else {
                Some(SingleBufferEventProducer::existing(self.buffer))
            }
        }
    }

    impl dirents_sink::Sealed for Sealed {
        fn open(self: Box<Self>) -> Box<dyn Any> {
            self
        }
    }
}
