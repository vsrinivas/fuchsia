// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    core::{
        pin::Pin,
        task::{Context, Poll},
    },
    futures::stream::{FusedStream, Stream, TryStream},
    pin_project::pin_project,
};

/// A stream that selects items from a [`Stream`] of [`Stream`]s.
///
/// `FlattenUnordered` is similar to [`futures::stream::Flatten`], but it polls
/// all the yielded streams to completion in an unordered fashion like
/// [`futures::stream::SelectAll`] instead of sequentially like `Flatten` does.
#[pin_project]
pub struct FlattenUnordered<S: Stream> {
    #[pin]
    select_all: futures::stream::SelectAll<S::Item>,
    #[pin]
    stream_of_streams: S,
}

impl<S> Stream for FlattenUnordered<S>
where
    S: Stream + FusedStream,
    S::Item: Stream + Unpin,
{
    type Item = <S::Item as Stream>::Item;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = self.project();
        let stream_of_streams = this.stream_of_streams;
        let mut select_all = this.select_all;

        let (terminated, needs_wake) = if stream_of_streams.is_terminated() {
            (true, false)
        } else {
            match stream_of_streams.poll_next(cx) {
                Poll::Ready(Some(new_stream)) => {
                    let () = select_all.push(new_stream);
                    (false, true)
                }
                Poll::Ready(None) => (true, false),
                Poll::Pending => (false, false),
            }
        };

        match select_all.poll_next(cx) {
            Poll::Ready(Some(item)) => {
                return Poll::Ready(Some(item));
            }
            Poll::Ready(None) => {
                if terminated {
                    return Poll::Ready(None);
                }
            }
            Poll::Pending => {}
        }

        if needs_wake {
            // When stream_of_streams was ready and we return pending, we must
            // wake the waker to force a new poll, otherwise we yield without a
            // waker properly registered on stream_of_streams.
            let () = cx.waker().wake_by_ref();
        }
        Poll::Pending
    }
}

impl<S> FusedStream for FlattenUnordered<S>
where
    S: Stream + FusedStream,
    S::Item: Stream + Unpin,
{
    fn is_terminated(&self) -> bool {
        let Self { stream_of_streams, select_all } = self;
        stream_of_streams.is_terminated() && select_all.is_empty()
    }
}

impl<S> FlattenUnordered<S>
where
    S: Stream + FusedStream,
    S::Item: Stream + Unpin,
{
    /// Creates a new `FlattenUnordered` from the provided stream of streams.
    pub fn new(stream_of_streams: S) -> Self {
        Self { stream_of_streams, select_all: futures::stream::SelectAll::new() }
    }
}

/// Extension trait to allow for easy creation of a [`FlattenUnordered`] stream
/// from a `Stream`.
pub trait FlattenUnorderedExt: Stream + Sized {
    /// Creates a [`FlattenUnordered`] stream from a stream of streams.
    fn flatten_unordered(self) -> FlattenUnordered<Self>;
}

impl<S> FlattenUnorderedExt for S
where
    S: Stream + FusedStream + Sized,
    S::Item: Stream + Unpin,
{
    fn flatten_unordered(self) -> FlattenUnordered<Self> {
        FlattenUnordered::new(self)
    }
}

/// A try stream that selects items from a [`TryStream`] of [`TryStream`]s.
///
/// Like [`FlattenUnordered`] but for [`TryStream`]s.
#[pin_project]
pub struct TryFlattenUnordered<S: TryStream> {
    #[pin]
    select_all: futures::stream::SelectAll<S::Ok>,
    #[pin]
    stream_of_streams: S,
}

impl<S> Stream for TryFlattenUnordered<S>
where
    S: TryStream + Stream<Item = Result<S::Ok, S::Error>> + FusedStream,
    S::Ok: TryStream<Error = S::Error>
        + Stream<Item = Result<<S::Ok as TryStream>::Ok, S::Error>>
        + Unpin,
{
    type Item = Result<<S::Ok as TryStream>::Ok, S::Error>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = self.project();
        let stream_of_streams = this.stream_of_streams;
        let mut select_all = this.select_all;

        let (terminated, needs_wake) = if stream_of_streams.is_terminated() {
            (true, false)
        } else {
            match stream_of_streams.try_poll_next(cx) {
                Poll::Ready(Some(Ok(new_stream))) => {
                    let () = select_all.push(new_stream);
                    (false, true)
                }
                Poll::Ready(Some(Err(err))) => {
                    return Poll::Ready(Some(Err(err)));
                }
                Poll::Ready(None) => (true, false),
                Poll::Pending => (false, false),
            }
        };

        match select_all.poll_next(cx) {
            Poll::Ready(Some(item)) => {
                return Poll::Ready(Some(item));
            }
            Poll::Ready(None) => {
                if terminated {
                    return Poll::Ready(None);
                }
            }
            Poll::Pending => {}
        }

        if needs_wake {
            // When stream_of_streams was ready and we return pending, we must
            // wake the waker to force a new poll, otherwise we yield without a
            // waker properly registered on stream_of_streams.
            let () = cx.waker().wake_by_ref();
        }
        Poll::Pending
    }
}

impl<S> TryFlattenUnordered<S>
where
    S: TryStream + FusedStream,
    S::Ok: TryStream<Error = S::Error> + Unpin,
{
    /// Creates a new `TryFlattenUnordered` from the provided stream of streams.
    pub fn new(stream_of_streams: S) -> Self {
        Self { stream_of_streams, select_all: futures::stream::SelectAll::new() }
    }
}

impl<S> FusedStream for TryFlattenUnordered<S>
where
    S: TryStream + FusedStream,
    S: TryStream + Stream<Item = Result<S::Ok, S::Error>> + FusedStream,
    S::Ok: TryStream<Error = S::Error>
        + Stream<Item = Result<<S::Ok as TryStream>::Ok, S::Error>>
        + Unpin,
{
    fn is_terminated(&self) -> bool {
        let Self { stream_of_streams, select_all } = self;
        stream_of_streams.is_terminated() && select_all.is_empty()
    }
}

/// Extension trait to allow for easy creation of a [`TryFlattenUnordered`] stream
/// from a `Stream`.
pub trait TryFlattenUnorderedExt: TryStream + Sized {
    /// Creates a [`TryFlattenUnordered`] stream from a stream of streams.
    fn try_flatten_unordered(self) -> TryFlattenUnordered<Self>;
}

impl<S> TryFlattenUnorderedExt for S
where
    S: TryStream + FusedStream + Sized,
    S::Ok: TryStream<Error = S::Error> + Unpin,
{
    fn try_flatten_unordered(self) -> TryFlattenUnordered<Self> {
        TryFlattenUnordered::new(self)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{
            FlattenUnordered, FlattenUnorderedExt as _, TryFlattenUnordered,
            TryFlattenUnorderedExt as _,
        },
        futures::{
            channel::mpsc, stream::FusedStream, FutureExt as _, Stream, StreamExt as _,
            TryStreamExt as _,
        },
    };

    enum FlattenUnorderedMarker {}
    enum TryFlattenUnorderedMarker {}

    trait TestMarker {
        type BaseStreamItem: PartialEq + std::fmt::Debug + Copy;
        type StreamOfStreamsItem;
        type TestStream: FusedStream + Stream<Item = Self::BaseStreamItem> + Unpin;

        fn make_item(v: u32) -> Self::BaseStreamItem;
        fn make_test_stream() -> (Self::TestStream, mpsc::UnboundedSender<Self::StreamOfStreamsItem>);
        fn add_inner_stream(
            sender: &mpsc::UnboundedSender<Self::StreamOfStreamsItem>,
        ) -> mpsc::UnboundedSender<Self::BaseStreamItem>;
    }

    impl TestMarker for FlattenUnorderedMarker {
        type BaseStreamItem = u32;
        type StreamOfStreamsItem = mpsc::UnboundedReceiver<Self::BaseStreamItem>;
        type TestStream = FlattenUnordered<mpsc::UnboundedReceiver<Self::StreamOfStreamsItem>>;

        fn make_item(v: u32) -> Self::BaseStreamItem {
            v
        }
        fn make_test_stream() -> (Self::TestStream, mpsc::UnboundedSender<Self::StreamOfStreamsItem>)
        {
            let (sender, receiver) = mpsc::unbounded();
            (receiver.flatten_unordered(), sender)
        }
        fn add_inner_stream(
            sender: &mpsc::UnboundedSender<Self::StreamOfStreamsItem>,
        ) -> mpsc::UnboundedSender<Self::BaseStreamItem> {
            let (s, r) = mpsc::unbounded();
            let () = sender.unbounded_send(r).unwrap();
            s
        }
    }

    impl TestMarker for TryFlattenUnorderedMarker {
        type BaseStreamItem = Result<u32, &'static str>;
        type StreamOfStreamsItem =
            Result<mpsc::UnboundedReceiver<Self::BaseStreamItem>, &'static str>;
        type TestStream = TryFlattenUnordered<mpsc::UnboundedReceiver<Self::StreamOfStreamsItem>>;

        fn make_item(v: u32) -> Self::BaseStreamItem {
            Ok(v)
        }
        fn make_test_stream() -> (Self::TestStream, mpsc::UnboundedSender<Self::StreamOfStreamsItem>)
        {
            let (sender, receiver) = mpsc::unbounded();
            (receiver.try_flatten_unordered(), sender)
        }
        fn add_inner_stream(
            sender: &mpsc::UnboundedSender<Self::StreamOfStreamsItem>,
        ) -> mpsc::UnboundedSender<Self::BaseStreamItem> {
            let (s, r) = mpsc::unbounded();
            let () = sender.unbounded_send(Ok(r)).unwrap();
            s
        }
    }

    macro_rules! dual_test {
        ($name:ident) => {
            paste::paste! {
                #[fuchsia_async::run_until_stalled(test)]
                async fn [< $name _flatten >] () {
                    $name::<FlattenUnorderedMarker>().await
                }
                #[fuchsia_async::run_until_stalled(test)]
                async fn [< $name _try_flatten >] () {
                    $name::<TryFlattenUnorderedMarker>().await
                }
            }
        };
    }

    dual_test!(selects_from_all_streams);
    async fn selects_from_all_streams<T: TestMarker>() {
        let (mut test_stream, sender) = T::make_test_stream();
        let s1 = T::add_inner_stream(&sender);
        let s2 = T::add_inner_stream(&sender);
        // Nothing to poll yet.
        assert_eq!(test_stream.next().now_or_never(), None);

        let item = T::make_item(2);
        let () = s2.unbounded_send(item).unwrap();
        assert_eq!(test_stream.next().await, Some(item));

        // Nothing more to poll.
        assert_eq!(test_stream.next().now_or_never(), None);

        let item = T::make_item(1);
        let () = s1.unbounded_send(item).unwrap();
        assert_eq!(test_stream.next().await, Some(item));
    }

    dual_test!(survives_inner_stream_termination);
    async fn survives_inner_stream_termination<T: TestMarker>() {
        let (mut test_stream, sender) = T::make_test_stream();
        for i in 0..3 {
            let s = T::add_inner_stream(&sender);
            let item = T::make_item(i);
            let () = s.unbounded_send(item).unwrap();
            std::mem::drop(s);
            assert_eq!(test_stream.next().await, Some(item));
            // Nothing more to poll.
            assert_eq!(test_stream.next().now_or_never(), None);
            // Not terminated.
            assert!(!test_stream.is_terminated());
        }
    }

    dual_test!(terminates_with_stream_of_streams);
    async fn terminates_with_stream_of_streams<T: TestMarker>() {
        let (mut test_stream, sender) = T::make_test_stream();
        let inner = T::add_inner_stream(&sender);
        let expected = (0u32..3)
            .into_iter()
            .map(|i| {
                let item = T::make_item(i);
                let () = inner.unbounded_send(item).unwrap();
                item
            })
            .collect::<Vec<_>>();
        std::mem::drop(inner);
        std::mem::drop(sender);
        let seen = test_stream.by_ref().collect::<Vec<_>>().await;
        assert_eq!(expected, seen);
        assert!(test_stream.is_terminated());
    }

    dual_test!(terminates_when_all_streams_end);
    async fn terminates_when_all_streams_end<T: TestMarker>() {
        let (mut test_stream, sender) = T::make_test_stream();
        let inner = T::add_inner_stream(&sender);
        std::mem::drop(sender);
        // Pending because the inner stream is still active.
        for _ in 0..3 {
            assert_eq!(test_stream.next().now_or_never(), None);
            assert!(!test_stream.is_terminated());
        }
        std::mem::drop(inner);
        assert_eq!(test_stream.next().await, None);
        assert!(test_stream.is_terminated());
    }

    dual_test!(doesnt_stall_on_empty_stream_yielded);
    async fn doesnt_stall_on_empty_stream_yielded<T: TestMarker>() {
        let (mut test_stream, sender) = T::make_test_stream();
        let inner = T::add_inner_stream(&sender);
        std::mem::drop(inner);
        let inner = T::add_inner_stream(&sender);
        let item = T::make_item(1);
        let () = inner.unbounded_send(item).unwrap();
        assert_eq!(test_stream.next().await, Some(item));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn try_flatten_unordered_short_circuits_errors() {
        let (mut test_stream, sender) = TryFlattenUnorderedMarker::make_test_stream();
        const ERR_STR: &'static str = "error";
        let () = sender.unbounded_send(Err(ERR_STR)).unwrap();
        assert_eq!(test_stream.try_next().await, Err(ERR_STR));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn makes_progress_with_infinite_flatten() {
        let flatten = futures::stream::iter(0..)
            .map(|i| futures::stream::iter(0..).map(move |j| (i, j)))
            .fuse()
            .flatten_unordered();
        const OBSERVATIONS: i32 = 3;
        let () = crate::fold::fold_while(flatten, (0, 0), |(pi, pj), (i, j)| {
            let pi = pi.max(i);
            let pj = pj.max(j);
            // We want to see at least the given number of different streams and
            // different items in some stream.
            let r = if pi >= OBSERVATIONS && pj >= OBSERVATIONS {
                crate::fold::FoldWhile::Done(())
            } else {
                crate::fold::FoldWhile::Continue((pi, pj))
            };
            futures::future::ready(r)
        })
        .await
        .short_circuited()
        .unwrap();
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn makes_progress_with_infinite_try_flatten() {
        type Result<T> = std::result::Result<T, ()>;
        let flatten = futures::stream::iter(0..)
            .map(|i| Result::Ok(futures::stream::iter(0..).map(move |j| Result::Ok((i, j)))))
            .fuse()
            .try_flatten_unordered();
        const OBSERVATIONS: i32 = 3;
        let () = crate::fold::try_fold_while(flatten, (0, 0), |(pi, pj), (i, j)| {
            let pi = pi.max(i);
            let pj = pj.max(j);
            // We want to see at least the given number of different streams and
            // different items in some stream.
            let r = if pi >= OBSERVATIONS && pj >= OBSERVATIONS {
                crate::fold::FoldWhile::Done(())
            } else {
                crate::fold::FoldWhile::Continue((pi, pj))
            };
            futures::future::ok(r)
        })
        .await
        .unwrap()
        .short_circuited()
        .unwrap();
    }
}
