// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_test as ftest, fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::LaunchError, fuchsia_zircon as zx, futures::prelude::*, futures::StreamExt,
    tracing::warn,
};

// Maps failures to call to fuchsia.test.Suite to a LaunchError value. This
// function should only be called iff an error was encountered when invoking a
// method on the `suite` object. Otherwise, `default_value` is returned.
pub fn map_suite_error_epitaph(
    suite: ftest::SuiteProxy,
    default_value: LaunchError,
) -> LaunchError {
    // Call now_or_never() so that test_manager isn't blocked on event not being ready.
    let next_evt_peek = suite.take_event_stream().next().now_or_never();
    match next_evt_peek {
        Some(Some(Err(fidl::Error::ClientChannelClosed {
            status: zx::Status::NOT_FOUND, ..
        }))) => LaunchError::InstanceCannotResolve,
        Some(Some(Err(fidl::Error::ClientChannelClosed { .. }))) => default_value,
        _ => {
            warn!("empty epitaph read from Suite: {:?}", next_evt_peek);
            LaunchError::InternalError
        }
    }
}

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
