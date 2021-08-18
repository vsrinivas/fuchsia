// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::channel::mpsc::{UnboundedReceiver, UnboundedSender};
use futures::{Stream, StreamExt};
use std::{
    cmp::Ordering,
    fmt::Debug,
    marker::Unpin,
    pin::Pin,
    task::{Context, Poll},
};
use tracing::trace;

pub type PinStream<I> = Pin<Box<dyn DebugStream<Item = I> + Send + 'static>>;

/// A Multiplexer takes multiple possibly-ordered streams and attempts to impose a sensible ordering
/// over the yielded items without risking starvation. New streams can be added to the multiplexer
/// by sending them on a channel.
pub struct Multiplexer<I> {
    // TODO(fxbug.dev/68257) explore using a BinaryHeap for sorting substreams
    current: Vec<SubStream<I>>,
    incoming: UnboundedReceiver<IncomingStream<PinStream<I>>>,
    incoming_is_live: bool,
}

impl<I> Multiplexer<I> {
    pub fn new() -> (Self, MultiplexerHandle<I>) {
        let (sender, incoming) = futures::channel::mpsc::unbounded();
        (Self { current: vec![], incoming, incoming_is_live: true }, MultiplexerHandle { sender })
    }

    /// Drain the incoming channel to be sure we have all live sub-streams available when
    /// considering ordering.
    fn integrate_incoming_sub_streams(&mut self, cx: &mut Context<'_>) {
        if self.incoming_is_live {
            loop {
                match self.incoming.poll_next_unpin(cx) {
                    // incoming has more for us right now
                    Poll::Ready(Some(IncomingStream::Next { name, stream })) => {
                        self.current.push(SubStream::new(name, stream));
                    }

                    // incoming has no more for us
                    Poll::Ready(Some(IncomingStream::Done)) | Poll::Ready(None) => {
                        self.incoming_is_live = false;
                        break;
                    }

                    // incoming has more for us, but not now
                    Poll::Pending => break,
                }
            }
        }
    }
}

impl<I: Ord + Unpin> Stream for Multiplexer<I> {
    type Item = I;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        // ensure the incoming channel is empty so that we're considering all streams here
        self.integrate_incoming_sub_streams(cx);

        // ensure we have the latest item cached from each stream that has results
        self.current.iter_mut().for_each(|s| s.poll_cache(cx));

        // ensure we only have substreams which can still yield values
        self.current.retain(SubStream::is_live);

        // sort by the cached latest item from each sub-stream
        self.current.sort_unstable_by(compare_sub_streams);

        if self.current.is_empty() && !self.incoming_is_live {
            // we don't have any live sub-streams and we're not getting any more
            Poll::Ready(None)
        } else if let Some(next) = self.current.get_mut(0).and_then(|c| c.cached.take()) {
            // get the front item among our substreams and return it if available
            Poll::Ready(Some(next))
        } else {
            // we're out of cached values but have sub-streams that claim to be pending
            Poll::Pending
        }
    }
}

/// A handle to a running multiplexer. Can be used to add new sub-streams to the multiplexer.
pub struct MultiplexerHandle<I> {
    sender: UnboundedSender<IncomingStream<PinStream<I>>>,
}

impl<I> MultiplexerHandle<I> {
    /// Send a new substream to the multiplexer. Returns `true` if it is still listening.
    pub fn send(&self, name: impl Into<String>, stream: PinStream<I>) -> bool {
        self.sender.unbounded_send(IncomingStream::Next { name: name.into(), stream }).is_ok()
    }

    /// Notify the multiplexer that no new sub-streams will be arriving.
    pub fn close(&self) {
        self.sender.unbounded_send(IncomingStream::Done).ok();
    }
}

enum IncomingStream<S> {
    Next { name: String, stream: S },
    Done,
}

/// A `SubStream` wraps an inner stream and keeps its latest value cached inline for comparison
/// with the cached values of other `SubStream`s, allowing for semi-ordered merging of streams.
#[derive(Debug)]
pub struct SubStream<I> {
    name: String,
    cached: Option<I>,
    inner_is_live: bool,
    inner: PinStream<I>,
}

impl<I> SubStream<I> {
    pub fn new(name: String, inner: PinStream<I>) -> Self {
        Self { name, cached: None, inner_is_live: true, inner }
    }
}

impl<I> SubStream<I> {
    /// Attempts to populate the inline cache of the latest stream value, if needed.
    fn poll_cache(&mut self, cx: &mut Context<'_>) {
        if self.cached.is_none() && self.inner_is_live {
            match self.inner.as_mut().poll_next(cx) {
                Poll::Ready(Some(item)) => self.cached = Some(item),
                Poll::Ready(None) => self.inner_is_live = false,
                Poll::Pending => (),
            }
        }
    }

    fn is_live(&self) -> bool {
        self.inner_is_live || self.cached.is_some()
    }
}

impl<I> Drop for SubStream<I> {
    fn drop(&mut self) {
        trace!(%self.name, "substream terminated");
    }
}

/// Compare two SubStreams so that streams with cached values come before those without cached
/// values, deferring to `I`'s `Ord` impl for those SubStreams with cached values.
fn compare_sub_streams<I: Ord>(a: &SubStream<I>, b: &SubStream<I>) -> Ordering {
    match (&a.cached, &b.cached) {
        (Some(a), Some(b)) => a.cmp(&b),
        (None, Some(_)) => Ordering::Greater,
        (Some(_), None) => Ordering::Less,
        (None, None) => Ordering::Equal,
    }
}

pub trait DebugStream: Debug {
    type Item;
    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>>;
}

impl<I, T> DebugStream for T
where
    T: Debug + Stream<Item = I>,
{
    type Item = I;
    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        Stream::poll_next(self, cx)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::{prelude::*, stream::iter as iter2stream};

    #[fuchsia::test]
    async fn empty_multiplexer_terminates() {
        let (mux, handle) = Multiplexer::<i32>::new();
        handle.close();
        let observed: Vec<i32> = mux.collect().await;
        let expected: Vec<i32> = vec![];
        assert_eq!(observed, expected);
    }

    #[fuchsia::test]
    async fn empty_input_streams_terminate() {
        let (mux, handle) = Multiplexer::<i32>::new();

        handle.send("empty1", Box::pin(iter2stream(vec![])) as PinStream<i32>);
        handle.send("empty2", Box::pin(iter2stream(vec![])) as PinStream<i32>);
        handle.send("empty3", Box::pin(iter2stream(vec![])) as PinStream<i32>);

        handle.close();
        let observed: Vec<i32> = mux.collect::<Vec<i32>>().await;
        let expected: Vec<i32> = vec![];
        assert_eq!(observed, expected);
    }

    #[fuchsia::test]
    async fn outputs_are_ordered() {
        let (mux, handle) = Multiplexer::<i32>::new();
        handle.send("first", Box::pin(iter2stream(vec![1, 3, 5, 7])) as PinStream<i32>);
        handle.send("second", Box::pin(iter2stream(vec![2, 4, 6, 8])) as PinStream<i32>);
        handle.send("third", Box::pin(iter2stream(vec![9, 10, 11])) as PinStream<i32>);

        handle.close();
        let observed: Vec<i32> = mux.collect::<Vec<i32>>().await;
        let expected: Vec<i32> = vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];
        assert_eq!(observed, expected);
    }

    #[fuchsia::test]
    async fn semi_sorted_substream_semi_sorted() {
        let (mux, handle) = Multiplexer::<i32>::new();
        handle.send("unordered", Box::pin(iter2stream(vec![1, 7, 3, 5])) as PinStream<i32>);
        handle.send("ordered", Box::pin(iter2stream(vec![2, 4, 6, 8])) as PinStream<i32>);

        handle.close();
        let observed: Vec<i32> = mux.collect::<Vec<i32>>().await;
        // we get the stream in a weird order because `unordered`'s 3 & 5 are held up behind 7
        let expected: Vec<i32> = vec![1, 2, 4, 6, 7, 3, 5, 8];
        assert_eq!(observed, expected);
    }

    #[fuchsia::test]
    async fn single_stream() {
        let (mut send, recv) = futures::channel::mpsc::unbounded();
        let (mut mux, handle) = Multiplexer::<i32>::new();
        handle.send("recv", Box::pin(recv) as PinStream<i32>);

        assert!(mux.next().now_or_never().is_none());
        send.unbounded_send(1).unwrap();
        assert_eq!(mux.next().await.unwrap(), 1);
        assert!(mux.next().now_or_never().is_none());

        send.unbounded_send(2).unwrap();
        send.unbounded_send(3).unwrap();
        send.unbounded_send(4).unwrap();
        send.unbounded_send(5).unwrap();
        send.unbounded_send(6).unwrap();
        send.disconnect();
        handle.close();

        let observed: Vec<i32> = mux.collect().await;
        assert_eq!(observed, vec![2, 3, 4, 5, 6]);
    }

    #[fuchsia::test]
    async fn two_streams_merged() {
        let (mut send1, recv1) = futures::channel::mpsc::unbounded();
        let (mut send2, recv2) = futures::channel::mpsc::unbounded();
        let (mut mux, handle) = Multiplexer::<i32>::new();
        handle.send("recv1", Box::pin(recv1) as PinStream<i32>);
        handle.send("recv2", Box::pin(recv2) as PinStream<i32>);

        assert!(mux.next().now_or_never().is_none());
        send1.unbounded_send(2).unwrap();
        send2.unbounded_send(1).unwrap();
        assert_eq!(mux.next().await.unwrap(), 1);
        assert_eq!(mux.next().await.unwrap(), 2);
        assert!(mux.next().now_or_never().is_none());

        send1.unbounded_send(2).unwrap();
        send2.unbounded_send(3).unwrap();
        send1.disconnect();
        assert_eq!(mux.next().await.unwrap(), 2);
        assert_eq!(mux.next().await.unwrap(), 3);
        assert!(mux.next().now_or_never().is_none());

        send2.unbounded_send(4).unwrap();
        send2.unbounded_send(5).unwrap();
        send2.disconnect();
        assert_eq!(mux.next().await.unwrap(), 4);
        assert_eq!(mux.next().await.unwrap(), 5);
        assert!(
            mux.next().now_or_never().is_none(),
            "multiplexer stays open even with current streams terminated"
        );

        handle.close();
        assert!(mux.next().await.is_none());
    }

    #[fuchsia::test]
    async fn new_sub_streams_are_merged() {
        let (mut send1, recv1) = futures::channel::mpsc::unbounded();
        let (mut send2, recv2) = futures::channel::mpsc::unbounded();
        let (mut send3, recv3) = futures::channel::mpsc::unbounded();
        let (mut mux, handle) = Multiplexer::<i32>::new();
        handle.send("recv1", Box::pin(recv1) as PinStream<i32>);
        handle.send("recv2", Box::pin(recv2) as PinStream<i32>);

        send3.unbounded_send(0).unwrap(); // this shouldn't show up until we add it to the mux below

        assert!(mux.next().now_or_never().is_none());
        send1.unbounded_send(2).unwrap();
        send2.unbounded_send(1).unwrap();
        assert_eq!(mux.next().await.unwrap(), 1);
        assert_eq!(mux.next().await.unwrap(), 2);
        assert!(mux.next().now_or_never().is_none());

        send1.unbounded_send(3).unwrap();
        handle.send("recv3", Box::pin(recv3) as PinStream<i32>);
        assert_eq!(mux.next().await.unwrap(), 0);
        assert_eq!(mux.next().await.unwrap(), 3);
        assert!(mux.next().now_or_never().is_none());

        handle.close();
        assert!(mux.next().now_or_never().is_none(), "open substreams hold the multiplexer open");

        send1.disconnect();
        send2.disconnect();
        send3.disconnect();
        assert!(mux.next().await.is_none(), "all substreams terminated, now we can close");
    }

    #[fuchsia::test]
    async fn snapshot_with_stopped_substream() {
        let (mut send1, recv1) = futures::channel::mpsc::unbounded();
        let (mut send2, recv2) = futures::channel::mpsc::unbounded();
        let (mut mux, handle) = Multiplexer::<i32>::new();
        send1.unbounded_send(1).unwrap();
        send1.disconnect();
        handle.send("recv1", Box::pin(recv1));

        send2.unbounded_send(2).unwrap();
        handle.send("recv2", Box::pin(recv2));
        handle.close();

        assert_eq!(mux.next().await.unwrap(), 1);
        assert_eq!(mux.next().await.unwrap(), 2);
        assert!(mux.next().now_or_never().is_none());

        send2.unbounded_send(3).unwrap();
        assert_eq!(mux.next().await.unwrap(), 3);
        assert!(mux.next().now_or_never().is_none());
        send2.disconnect();
        assert!(mux.next().await.is_none(), "all substreams terminated, now we can close");
    }
}
