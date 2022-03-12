// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Watchers handles a list of watcher connections attached to a directory.  Watchers as described
//! in fuchsia.io.

pub mod event_producers;

mod watcher;
pub use watcher::Controller;

use crate::{
    directory::{
        entry_container::{Directory, DirectoryWatcher},
        watchers::event_producers::EventProducer,
    },
    execution_scope::ExecutionScope,
};

use fidl_fuchsia_io as fio;

use {slab::Slab, std::sync::Arc};

/// Wraps all watcher connections observing one directory.  The directory is responsible for
/// calling [`Self::add()`] and [`Self::send_event()`] method when appropriate to make sure
/// watchers are observing a consistent view.
pub struct Watchers(Slab<Arc<Controller>>);

impl Watchers {
    /// Constructs a new Watchers instance with no connected watchers.
    pub fn new() -> Self {
        Watchers(Slab::new())
    }

    /// Connects a new watcher (connected over the `channel`) to the list of watchers.  It is the
    /// responsibility of the caller to also send `WATCH_EVENT_EXISTING` and `WatchMask::IDLE`
    /// events on the returned [`Controller`] to the newly connected watcher using the
    /// [`Self::send_event`] methods.  This `mask` is the event mask this watcher has requested.
    ///
    /// Return value of `None` means the executor did not accept a new task, so the watcher has
    /// been dropped.
    ///
    /// NOTE The reason `add` can not send both events on its own by consuming an
    /// [`EventProducer`] is because a lazy directory needs async context to generate a list of
    /// it's entries.  Meaning we need a async version of the [`EventProducer`] - and that is a lot
    /// of additional managing of functions and state.  Traits do not support async methods yet, so
    /// we would need to manage futures returned by the [`EventProducer`] methods explicitly.
    /// Plus, for the [`crate::directory::simple::Simple`] directory it is all unnecessary.
    #[must_use = "Caller of add() must send WATCH_EVENT_EXISTING and fio::WatchMask::IDLE on the \
                  returned controller"]
    pub fn add(
        &mut self,
        scope: ExecutionScope,
        directory: Arc<dyn Directory>,
        mask: fio::WatchMask,
        watcher: DirectoryWatcher,
    ) -> Arc<Controller> {
        let entry = self.0.vacant_entry();
        let key = entry.key();

        let done = move || {
            // Add tests.
            // TODO(72292)
            directory.unregister_watcher(key);
        };

        let controller = Arc::new(watcher::new(scope, mask, watcher, done));
        entry.insert(controller).clone()
    }

    /// Informs all the connected watchers about the specified event.  While `mask` and `event`
    /// carry the same information, as they are represented by `WatchMask::*` and `WATCH_EVENT_*`
    /// constants in fuchsia.io, it is easier when both forms are provided.  `mask` is used to
    /// filter out those watchers that did not request for observation of this event and `event` is
    /// used to construct the event object.  The method will operate correctly only if `mask` and
    /// `event` match.
    ///
    /// In case of a communication error with any of the watchers, connection to this watcher is
    /// closed.
    pub fn send_event(&mut self, producer: &mut dyn EventProducer) {
        while producer.prepare_for_next_buffer() {
            let mut consumed_any = false;

            for (_key, controller) in self.0.iter() {
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
