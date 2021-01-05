// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::{future::BoxFuture, prelude::*, task::Poll};

/// Wraps a future in order to allow reading/writing of "metadata" while
/// the future is pending. When the future finishes, it will resolve with
/// a tuple: (inner_fut::Output, metadata).
pub struct FutureWithMetadata<F, M> {
    inner_fut: BoxFuture<'static, F>,
    pub metadata: M,
}

impl<F, M> FutureWithMetadata<F, M>
where
    M: Unpin + Clone,
{
    pub fn new(metadata: M, future: BoxFuture<'static, F>) -> Self {
        Self { metadata, inner_fut: future }
    }
}

impl<F, M> Future for FutureWithMetadata<F, M>
where
    M: Unpin + Clone,
{
    type Output = (F, M);
    fn poll(
        mut self: std::pin::Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
    ) -> std::task::Poll<<Self as Future>::Output> {
        match self.inner_fut.poll_unpin(cx) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(val) => Poll::Ready((val, self.metadata.clone())),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        futures::{future, task::Poll},
        pin_utils::pin_mut,
        wlan_common::assert_variant,
    };

    #[test]
    fn assign_and_read_metadata_in_future_output() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        #[derive(Debug, Clone)]
        struct TestMetadata {
            has_been_written: bool,
        }

        // Create a future
        let test_future = FutureWithMetadata::new(
            TestMetadata { has_been_written: false },
            future::ready("future result").boxed(),
        );
        pin_mut!(test_future);

        // Ensure the initial value is as expected
        assert_eq!(test_future.metadata.has_been_written, false);

        // Mutate the metadata
        test_future.metadata.has_been_written = true;

        // The future should resolve with the metadata
        assert_variant!(exec.run_until_stalled(&mut test_future), Poll::Ready((fut_result, metadata)) => {
            assert_eq!(metadata.has_been_written, true);
            assert_eq!(fut_result, "future result");
        });
    }
}
