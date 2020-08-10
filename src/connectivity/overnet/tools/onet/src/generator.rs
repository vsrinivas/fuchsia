// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async::Task;
use futures::{channel::mpsc, prelude::*};
use std::pin::Pin;
use std::task::{Context, Poll};

/// Take a function that can emit to a channel, and turn it into a Stream instance.
pub struct Generator<T> {
    rx: mpsc::Receiver<T>,
    _task: Task<()>,
}

impl<T> Generator<T> {
    pub fn new<F: FnOnce(mpsc::Sender<T>) -> Fut, Fut: 'static + Send + Future<Output = ()>>(
        f: F,
    ) -> Self {
        let (tx, rx) = mpsc::channel(0);
        Self { rx, _task: Task::spawn(f(tx)) }
    }
}

impl<T> Stream for Generator<T> {
    type Item = T;
    fn poll_next(mut self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.rx.poll_next_unpin(ctx)
    }
}
