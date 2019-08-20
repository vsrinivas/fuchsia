// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Watchers handles a list of watcher connections attached to a directory.  Watchers as described
//! in io.fidl.

pub mod event_producers;

mod watcher;

use crate::{
    directory::{
        connection::DirectoryEntryContainer,
        watchers::event_producers::{EventProducer, SingleNameEventProducer},
    },
    execution_scope::ExecutionScope,
};

use {fidl_fuchsia_io::WATCH_EVENT_EXISTING, fuchsia_async::Channel, slab::Slab, std::sync::Arc};

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
    /// watcher will receive the `WATCH_EVENT_EXISTING` event with all the names provided by the
    /// `existing_event` producer.  This producer must be configured to use `WATCH_EVENT_EXISTING`
    /// as the event for buffer construction - `add` will call [`EventProducer::event()`] to check
    /// that.  `mask` is the event mask this watcher has requested.
    pub fn add<TraversalPosition>(
        &mut self,
        scope: ExecutionScope,
        directory: Arc<dyn DirectoryEntryContainer<TraversalPosition>>,
        existing_event: &mut dyn EventProducer,
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

        debug_assert!(existing_event.event() == WATCH_EVENT_EXISTING);

        controller.send_event(existing_event);
        controller.send_event(&mut SingleNameEventProducer::idle());
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
    pub fn send_event(&mut self, producer: &mut dyn EventProducer) {
        while producer.prepare_for_next_buffer() {
            let mut consumed_any = false;

            for (_key, controller) in self.0.iter_mut() {
                controller.send_buffer(producer.mask(), || {
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
