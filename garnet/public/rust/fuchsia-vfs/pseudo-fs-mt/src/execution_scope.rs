// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Values of this type represent "execution scopes" used by the library to give fine grained
//! control of the lifetimes of the tasks associated with particular connections.  When a new
//! connection is attached to a pseudo directory tree, an execution scope is provided.  This scope
//! is then used to start any tasks related to this connection.  All connections opened as a result
//! of operations on this first connection will also use the same scope, as well as any tasks
//! related to those connections.
//!
//! This way, it is possible to control the lifetime of a group of connections.  All connections
//! and their tasks can be shutdown by calling `shutdown` method on the scope that is hosting them.
//! Scope will also shutdown all the tasks when it goes out of scope.
//!
//! Implementation wise, execution scope is just a proxy, that forwards all the tasks to an actual
//! executor, provided as an instance of a [`Spawn`] trait.

use {
    futures::{
        future::{AbortHandle, Abortable, Aborted, BoxFuture},
        task::{self, Spawn},
        Future, FutureExt,
    },
    parking_lot::Mutex,
    slab::Slab,
    std::{ops::Drop, sync::Arc},
};

pub type SpawnError = task::SpawnError;

/// An execution scope that is hosting tasks for a group of connections.  See the module level
/// documentation for details.
///
/// Actual execution will be delegated to an "upstream" executor - something that implements
/// [`Spawn`].  In a sense, this is somewhat of an analog of a multithreaded capable
/// [`FuturesUnordered`], but this some additional functionality specific to the pseudo-fs-mt
/// library.
pub struct ExecutionScope {
    inner: Arc<Mutex<Inner>>,
}

struct Inner {
    upstream: Box<dyn Spawn + Send>,
    running: Slab<AbortHandle>,
}

impl ExecutionScope {
    /// Constructs a new execution scope, wrapping the specified executor.
    pub fn new(upstream: Box<dyn Spawn + Send>) -> Self {
        let inner = Arc::new(Mutex::new(Inner { upstream, running: Slab::new() }));
        ExecutionScope { inner }
    }

    /// Sends a `task` to be executed in this execution scope.  This is very similar to
    /// [`Spawn::spawn_obj`] with a minor difference that `self` reference is not exclusive.
    ///
    /// For the "pseudo-fs-mt" library it is more convenient that this method allows non-exclusive
    /// access.  And as the implementation is employing internal mutability there are no downsides.
    /// This way `ExecutionScope` can actually also implement [`Spawn`] - it just was not necessary
    /// for now.
    pub fn spawn(&self, task: BoxFuture<'static, ()>) -> Result<(), SpawnError> {
        let mut this = self.inner.lock();
        let wrap_task = this.wrap_task(self.inner.clone(), task).into();
        this.upstream.spawn_obj(wrap_task)
    }

    pub fn shutdown(&self) {
        let mut this = self.inner.lock();
        this.shutdown();
    }
}

impl Clone for ExecutionScope {
    fn clone(&self) -> Self {
        ExecutionScope { inner: self.inner.clone() }
    }
}

impl Inner {
    fn wrap_task(
        &mut self,
        inner: Arc<Mutex<Inner>>,
        task: impl Future<Output = ()> + Send + 'static,
    ) -> BoxFuture<'static, ()> {
        let (handle, registration) = AbortHandle::new_pair();
        let running_entry = self.running.insert(handle);
        let track_completion = async move {
            await!(task);
            let mut this = inner.lock();
            this.running.remove(running_entry);
        };

        Box::pin(Abortable::new(track_completion, registration).map(|res| match res {
            Ok(()) => (),
            Err(Aborted {}) => (),
        }))
    }

    fn shutdown(&mut self) {
        for handle in self.running.drain() {
            handle.abort();
        }
    }
}

impl Drop for Inner {
    fn drop(&mut self) {
        self.shutdown();
    }
}
