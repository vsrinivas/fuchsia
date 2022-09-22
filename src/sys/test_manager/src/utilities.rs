// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    futures::{prelude::*, StreamExt},
    tracing::info,
};

/// Convert iterator fidl method into stream of events.
/// ie convert
/// fidl_method() -> Future<Result<Vec<T>, E>>
/// to
/// Stream<Item=Result<T, E>>
pub fn stream_fn<F, T, E, Fut>(query_fn: F) -> impl Stream<Item = Result<T, E>>
where
    F: 'static + FnMut() -> Fut + Unpin + Send + Sync,
    Fut: Future<Output = Result<Vec<T>, E>> + Unpin + Send + Sync,
{
    futures::stream::try_unfold(query_fn, |mut query_fn| async move {
        Ok(Some((query_fn().await?, query_fn)))
    })
    .try_take_while(|vec| futures::future::ready(Ok(!vec.is_empty())))
    .map_ok(|vec| futures::stream::iter(vec).map(Ok))
    .try_flatten()
}

/// A struct which logs a set string when it drops out of scope.
pub struct LogOnDrop(pub &'static str);

impl std::ops::Drop for LogOnDrop {
    fn drop(&mut self) {
        info!("{}", self.0);
    }
}
