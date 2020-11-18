// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_utils::stream::{StreamMap, Tagged, WithTag},
    fuchsia_bluetooth::types::{HostId, PeerId},
    futures::{
        future::BoxFuture,
        stream::{FuturesUnordered, Stream},
    },
    pin_utils::unsafe_pinned,
    std::{
        collections::HashMap,
        pin::Pin,
        task::{Context, Poll},
    },
};

/// A Stream of outstanding Pairing Requests, indexed by Host. When polled, the Stream impl will
/// yield the next available completed request, when ready. `T` is the response type returned on
/// completion of a request.
pub struct PairingRequests<T> {
    inner: StreamMap<HostId, FuturesUnordered<Tagged<PeerId, BoxFuture<'static, T>>>>,
}

impl<T: Unpin> Unpin for PairingRequests<T> {}

impl<T> PairingRequests<T> {
    /// Create a new empty PairingRequests<T>
    pub fn empty() -> PairingRequests<T> {
        PairingRequests { inner: StreamMap::empty() }
    }
    /// Insert a new pending request future, identified by HostId and PeerId
    pub fn insert(&mut self, host: HostId, peer: PeerId, request: BoxFuture<'static, T>) {
        self.inner
            .inner()
            .entry(host)
            .or_insert(Box::pin(FuturesUnordered::new()))
            .push(request.tagged(peer))
    }
    /// Remove all pending requests for a given host, returning the PeerIds of those requests
    pub fn remove_host_requests(&mut self, host: HostId) -> Option<Vec<PeerId>> {
        self.inner.remove(&host).map(|mut futs| futs.iter_mut().map(|f| f.tag()).collect())
    }
    /// Remove all pending requests, returning the PeerIds of those requests
    pub fn take_all_requests(&mut self) -> HashMap<HostId, Vec<PeerId>> {
        self.inner
            .inner()
            .drain()
            .map(|(host, mut futs)| (host, futs.iter_mut().map(|f| f.tag()).collect()))
            .collect()
    }

    // It is safe to take a pinned projection to `inner` as:
    // * PairingRequests does not implement `Drop`
    // * PairingRequests only implements Unpin if `inner` is Unpin.
    // * PairingRequests is not #[repr(packed)].
    // see: pin_utils::unsafe_pinned docs for details
    unsafe_pinned!(
        inner: StreamMap<HostId, FuturesUnordered<Tagged<PeerId, BoxFuture<'static, T>>>>
    );
}

impl<T> Stream for PairingRequests<T> {
    type Item = (PeerId, T);

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.inner().poll_next(cx)
    }
}
