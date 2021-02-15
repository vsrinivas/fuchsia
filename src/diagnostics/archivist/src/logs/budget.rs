// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{archivist::ComponentRemover, logs::container::LogsArtifactsContainer};
use parking_lot::Mutex;
use std::sync::{Arc, Weak};
use tracing::debug;

#[derive(Clone)]
pub struct BudgetManager {
    state: Arc<Mutex<BudgetState>>,
}

impl BudgetManager {
    pub fn new(capacity: usize) -> Self {
        Self {
            state: Arc::new(Mutex::new(BudgetState {
                capacity,
                current: 0,
                containers: vec![],
                remover: Default::default(),
            })),
        }
    }

    pub fn set_remover(&self, remover: ComponentRemover) {
        self.state.lock().remover = remover;
    }

    pub fn add_container(&self, container: Arc<LogsArtifactsContainer>) {
        self.state.lock().containers.push(container);
    }

    pub fn handle(&self) -> BudgetHandle {
        BudgetHandle { state: Arc::downgrade(&self.state) }
    }
}

struct BudgetState {
    current: usize,
    capacity: usize,
    remover: ComponentRemover,

    /// Log containers are stored in a `Vec` which is regularly sorted instead of a `BinaryHeap`
    /// because `BinaryHeap`s are broken with interior mutability in the contained type which would
    /// affect the `Ord` impl's results.
    ///
    /// To use a BinaryHeap, we would have to make the container's `Ord` impl call
    /// `oldest_timestamp()`, but the value of that changes every time `pop()` is called. At the
    /// time of writing, `pop()` does not require a mutable reference. While it's only called from
    /// this module, we don't have a way to statically enforce that. This means that in a
    /// future change we could introduce incorrect and likely flakey behavior without any warning.
    containers: Vec<Arc<LogsArtifactsContainer>>,
}

impl BudgetState {
    fn allocate(&mut self, size: usize) {
        self.current += size;

        while self.current > self.capacity {
            // find the container with the oldest log message
            self.containers.sort_unstable_by_key(|c| c.oldest_timestamp().unwrap_or(std::i64::MAX));

            let container_with_oldest = self
                .containers
                .get(0)
                .expect("containers are added to budget before they can call allocate")
                .clone();
            let oldest_message = container_with_oldest
                .pop()
                .expect("if we need to free space, we have messages to remove");
            self.current -= oldest_message.metadata.size_bytes;
        }

        // now we need to remove any containers that are no longer needed. this will usually only
        // fire for components from which we've just dropped a message, but it also serves to clean
        // up containers which may not have been removable when we first received the stop event.

        // the below code is ~equivalent to the unstable drain_filter
        // https://doc.rust-lang.org/std/vec/struct.Vec.html#method.drain_filter
        let mut i = 0;
        while i != self.containers.len() {
            if !self.containers[i].should_retain() {
                let container = self.containers.remove(i);
                let identity = &container.identity;
                debug!(%identity, "Removing now that we've popped the last message.");
                self.remover.maybe_remove_component(identity);
            } else {
                i += 1;
            }
        }
    }
}

pub struct BudgetHandle {
    /// We keep a weak pointer to the budget state to avoid this ownership cycle:
    ///
    /// `BudgetManager -> BudgetState -> LogsArtifactsContainer -> BudgetHandle -> BudgetState`
    state: Weak<Mutex<BudgetState>>,
}

impl BudgetHandle {
    pub fn allocate(&self, size: usize) {
        self.state.upgrade().expect("budgetmanager outlives all containers").lock().allocate(size);
    }
}
