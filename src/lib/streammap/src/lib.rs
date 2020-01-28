// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! An unordered map of streams.

#[macro_use]
extern crate rental;

#[cfg(test)]
mod test;

use futures::{
    lock::{Mutex, MutexLockFuture},
    stream::{FusedStream, SelectAll},
    Stream,
};
use rental::*;
use std::collections::HashMap;
use std::future::Future;
use std::hash::Hash;
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll};

rental! {
  mod rentals {
    use super::*;

    #[rental]
    pub struct ArcMutexWithLockFuture<T: 'static> {
        store: Arc<Mutex<T>>,
        lock: Option<MutexLockFuture<'store, T>>,
    }
  }
}

struct StreamEntry<K: 'static, St: 'static> {
    key: K,
    lock: rentals::ArcMutexWithLockFuture<HashMap<K, St>>,
}

impl<K: 'static, St: 'static> Unpin for StreamEntry<K, St> {}

impl<K: Copy + Eq + Hash + 'static, St: Stream + FusedStream + Unpin + 'static> Stream
    for StreamEntry<K, St>
{
    type Item = St::Item;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let key = self.key;

        self.lock.rent_all_mut(|store_and_lock| {
            let mut lock_fut = match store_and_lock.lock.take() {
                Some(lock_fut) => lock_fut,
                None => store_and_lock.store.lock(),
            };
            match Pin::new(&mut lock_fut).poll(cx) {
                Poll::Pending => {
                    *store_and_lock.lock = Some(lock_fut);
                    Poll::Pending
                }
                Poll::Ready(mut guard) => {
                    let stream = if let Some(stream) = guard.get_mut(&key) {
                        stream
                    } else {
                        return Poll::Ready(None);
                    };

                    if stream.is_terminated() {
                        guard.remove(&key);
                        return Poll::Ready(None);
                    }

                    match Pin::new(stream).poll_next(cx) {
                        Poll::Ready(None) => {
                            guard.remove(&key);
                            Poll::Ready(None)
                        }
                        poll_result => poll_result,
                    }
                }
            }
        })
    }
}

/// A map of streams.
///
/// When polled as a stream, this will yield the element of whichever stream has an element ready
/// first, similar to `SelectAll`.
///
/// The map will never terminate as a stream. When there are no elements, the stream will pend.
///
/// Streams added to the map may be removed or modified using the key with which they were inserted.
pub struct StreamMap<K: 'static, St: 'static> {
    store: Arc<Mutex<HashMap<K, St>>>,
    streams: SelectAll<StreamEntry<K, St>>,
}

impl<K: Copy + Eq + Hash + 'static, St: Stream + FusedStream + Unpin + 'static> Default
    for StreamMap<K, St>
{
    fn default() -> Self {
        StreamMap { store: Arc::default(), streams: SelectAll::new() }
    }
}

impl<K: Copy + Eq + Hash + 'static, St: Stream + FusedStream + Unpin + 'static> StreamMap<K, St> {
    /// Creates a new stream map with no streams, which will pend.
    pub fn new() -> Self {
        Self::default()
    }

    /// Adds a stream to the map. It will be polled the next time the map is polled.
    /// The stream may be removed or modified later using the same key.
    ///
    /// If another stream was already present under this same key, it is returned.
    pub async fn insert(&mut self, key: K, stream: St) -> Option<St> {
        self.streams.push(StreamEntry {
            key,
            lock: rentals::ArcMutexWithLockFuture::new(self.store.clone(), |_| None),
        });
        self.store.lock().await.insert(key, stream)
    }

    /// Ceases polling and moves to caller the stream added under this key, if there was one.
    pub async fn remove(&self, key: K) -> Option<St> {
        self.store.lock().await.remove(&key)
    }

    /// Executes `f` on a mutable reference to the stream added under the given key, if it exists.
    /// Returns whether the function was executed.
    pub async fn with_elem(&self, key: K, f: impl FnOnce(&mut St)) -> bool {
        match self.store.lock().await.get_mut(&key) {
            Some(e) => {
                f(e);
                true
            }
            None => false,
        }
    }

    /// Execute `f` on an immutable reference to every stream.
    pub async fn for_each_stream(&self, mut f: impl FnMut(K, &St)) {
        self.store.lock().await.iter().for_each(move |(k, st)| f(*k, st));
    }

    /// Execute `f` on a mutable reference to every stream.
    pub async fn for_each_stream_mut(&mut self, mut f: impl FnMut(K, &mut St)) {
        self.store.lock().await.iter_mut().for_each(move |(k, st)| f(*k, st));
    }

    #[cfg(test)]
    pub fn store(&self) -> Arc<Mutex<HashMap<K, St>>> {
        self.store.clone()
    }
}

impl<K: Copy + Eq + Hash, St: Stream + FusedStream + Unpin> Stream for StreamMap<K, St> {
    type Item = St::Item;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        match Pin::new(&mut self.streams).poll_next(cx) {
            Poll::Ready(None) => Poll::Pending,
            poll_result => poll_result,
        }
    }
}

impl<K: Copy + Eq + Hash, St: Stream + FusedStream + Unpin> FusedStream for StreamMap<K, St> {
    fn is_terminated(&self) -> bool {
        false
    }
}
