// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Asynchronous generator-like functionality in stable Rust.

use {
    futures::{
        channel::mpsc,
        future::FusedFuture,
        prelude::*,
        stream::FusedStream,
        task::{Context, Poll},
    },
    pin_project::pin_project,
    std::pin::Pin,
};

/// Produces an asynchronous `Stream` of [`GeneratorState<I, R>`] by invoking the given closure
/// with a handle that can be used to yield items.
///
/// The returned `Stream` will produce a GeneratorState::Yielded variant for all yielded items
/// from the asynchronous task, followed by a single GeneratorState::Complete variant, which will
/// always be present as the final element in the stream.
pub fn generate<'a, I, R, C, F>(cb: C) -> Generator<F, I, R>
where
    C: FnOnce(Yield<I>) -> F,
    F: Future<Output = R> + 'a,
    I: Send + 'static,
    R: Send + 'static,
{
    let (send, recv) = mpsc::channel(0);
    Generator { task: cb(Yield(send)).fuse(), stream: recv, res: None }
}

/// Control handle to yield items to the coroutine.
pub struct Yield<I>(mpsc::Sender<I>);

impl<I> Yield<I>
where
    I: Send + 'static,
{
    /// Yield a single item to the coroutine, waiting for it to receive the item.
    pub fn yield_(&mut self, item: I) -> impl Future<Output = ()> + '_ {
        // Ignore errors as Generator never drops the stream before the task.
        self.0.send(item).map(|_| ())
    }

    /// Yield multiple items to the coroutine, waiting for it to receive all of them.
    pub fn yield_all<S>(&mut self, items: S) -> impl Future<Output = ()> + '_
    where
        S: IntoIterator<Item = I>,
        S::IntoIter: 'static,
    {
        let mut items = futures::stream::iter(items.into_iter().map(Ok));
        async move {
            let _ = self.0.send_all(&mut items).await;
        }
    }
}

/// Emitted state from an async generator.
#[derive(Debug, PartialEq, Eq)]
pub enum GeneratorState<I, R> {
    /// The async generator yielded a value.
    Yielded(I),

    /// The async generator completed with a return value.
    Complete(R),
}

impl<I, R> GeneratorState<I, R> {
    fn into_yielded(self) -> Option<I> {
        match self {
            GeneratorState::Yielded(item) => Some(item),
            _ => None,
        }
    }

    fn into_complete(self) -> Option<R> {
        match self {
            GeneratorState::Complete(res) => Some(res),
            _ => None,
        }
    }
}

/// An asynchronous generator.
#[pin_project]
#[derive(Debug)]
pub struct Generator<F, I, R>
where
    F: Future<Output = R>,
{
    #[pin]
    task: future::Fuse<F>,
    #[pin]
    stream: mpsc::Receiver<I>,
    res: Option<R>,
}

impl<F, I, E> Generator<F, I, Result<(), E>>
where
    F: Future<Output = Result<(), E>>,
{
    /// Transforms this stream of `GeneratorState<I, Result<(), E>>` into a stream of `Result<I, E>`.
    pub fn into_try_stream(self) -> impl Stream<Item = Result<I, E>> {
        self.filter_map(|state| {
            future::ready(match state {
                GeneratorState::Yielded(i) => Some(Ok(i)),
                GeneratorState::Complete(Ok(())) => None,
                GeneratorState::Complete(Err(e)) => Some(Err(e)),
            })
        })
    }
}

impl<F, I, R> Generator<F, I, R>
where
    F: Future<Output = R>,
{
    /// Discards all intermediate values produced by this generator, producing just the final result.
    pub fn into_complete(self) -> impl Future<Output = R> {
        async move {
            let s = self.filter_map(|state| future::ready(state.into_complete()));
            futures::pin_mut!(s);

            // Generators always yield a complete item as the final element once the task
            // completes.
            s.next().await.unwrap()
        }
    }
}

impl<F, I> Generator<F, I, ()>
where
    F: Future<Output = ()>,
{
    /// Filters the states produced by this generator to only include intermediate yielded values,
    /// discarding the final result.
    pub fn into_yielded(self) -> impl Stream<Item = I> + FusedStream {
        self.filter_map(|state| future::ready(state.into_yielded()))
    }
}

impl<F, I, R> Stream for Generator<F, I, R>
where
    F: Future<Output = R>,
{
    type Item = GeneratorState<I, R>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = self.project();

        // Always poll the task first to make forward progress and maybe push an item into the
        // channel.
        let mut task_done = this.task.is_terminated();
        if let Poll::Ready(res) = this.task.poll(cx) {
            // This stream might not be ready for the final result yet, store it for later.
            this.res.replace(res);
            task_done = true;
        }

        // Return anything available from the stream, ignoring stream termination to let the task
        // termination yield the last value.
        if !this.stream.is_terminated() {
            match this.stream.poll_next(cx) {
                Poll::Pending => return Poll::Pending,
                Poll::Ready(Some(item)) => return Poll::Ready(Some(GeneratorState::Yielded(item))),
                Poll::Ready(None) => {}
            }
        }

        if !task_done {
            return Poll::Pending;
        }

        // Flush the final result once all tasks are done.
        match this.res.take() {
            Some(res) => Poll::Ready(Some(GeneratorState::Complete(res))),
            None => Poll::Ready(None),
        }
    }
}

impl<F, I, R> FusedStream for Generator<F, I, R>
where
    F: Future<Output = R>,
{
    fn is_terminated(&self) -> bool {
        self.task.is_terminated() && self.stream.is_terminated() && self.res.is_none()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::executor::block_on;
    use std::sync::atomic;

    /// Returns a future that yields to the executor once before completing.
    fn yield_once() -> impl Future<Output = ()> {
        let mut done = false;
        future::poll_fn(move |cx: &mut Context<'_>| {
            if !done {
                done = true;
                cx.waker().wake_by_ref();
                Poll::Pending
            } else {
                Poll::Ready(())
            }
        })
    }

    #[derive(Debug, Default)]
    struct Counter(atomic::AtomicU32);

    impl Counter {
        fn inc(&self) {
            self.0.fetch_add(1, atomic::Ordering::SeqCst);
        }

        fn take(&self) -> u32 {
            self.0.swap(0, atomic::Ordering::SeqCst)
        }
    }

    #[test]
    fn generator_waits_for_item_to_yield() {
        let counter = Counter::default();

        let s = generate(|mut co| {
            let counter = &counter;
            async move {
                counter.inc();
                co.yield_("first").await;

                // This yield should not be observable by the stream, but the extra increment will
                // be.
                counter.inc();
                yield_once().await;

                counter.inc();
                co.yield_("second").await;

                drop(co);
                yield_once().await;

                counter.inc();
            }
        });

        block_on(async {
            futures::pin_mut!(s);

            assert_eq!(counter.take(), 0);

            assert_eq!(s.next().await, Some(GeneratorState::Yielded("first")));
            assert_eq!(counter.take(), 1);

            assert_eq!(s.next().await, Some(GeneratorState::Yielded("second")));
            assert_eq!(counter.take(), 2);

            assert_eq!(s.next().await, Some(GeneratorState::Complete(())));
            assert_eq!(counter.take(), 1);

            assert_eq!(s.next().await, None);
            assert_eq!(counter.take(), 0);
        });
    }

    #[test]
    fn yield_all_yields_all() {
        let s = generate(|mut co| async move {
            co.yield_all(1u32..4).await;
            co.yield_(42).await;
        });

        let res = block_on(s.collect::<Vec<GeneratorState<u32, ()>>>());

        assert_eq!(
            res,
            vec![
                GeneratorState::Yielded(1),
                GeneratorState::Yielded(2),
                GeneratorState::Yielded(3),
                GeneratorState::Yielded(42),
                GeneratorState::Complete(()),
            ]
        );
    }

    #[test]
    fn fused_impl() {
        let s = generate(|mut co| async move {
            co.yield_(1u32).await;
            drop(co);

            yield_once().await;

            "done"
        });

        block_on(async {
            futures::pin_mut!(s);

            assert!(!s.is_terminated());
            assert_eq!(s.next().await, Some(GeneratorState::Yielded(1)));

            assert!(!s.is_terminated());
            assert_eq!(s.next().await, Some(GeneratorState::Complete("done")));

            // FusedStream's is_terminated typically returns false after yielding None to indicate
            // no items are left, but it is also valid to return true when the stream is going to
            // not make further progress.
            assert!(s.is_terminated());
            assert_eq!(s.next().await, None);

            assert!(s.is_terminated());
        });
    }

    #[test]
    fn into_try_stream_transposes_generator_states() {
        let s = generate(|mut co| async move {
            co.yield_(1u8).await;
            co.yield_(2u8).await;

            Result::<(), &'static str>::Err("oops")
        })
        .into_try_stream();

        let res = block_on(s.collect::<Vec<Result<u8, &'static str>>>());

        assert_eq!(res, vec![Ok(1), Ok(2), Err("oops")]);
    }

    #[test]
    fn into_try_stream_eats_unit_success() {
        let s = generate(|mut co| async move {
            co.yield_(1u8).await;
            co.yield_(2u8).await;

            Result::<(), &'static str>::Ok(())
        })
        .into_try_stream();

        let res = block_on(s.collect::<Vec<Result<u8, &'static str>>>());

        assert_eq!(res, vec![Ok(1), Ok(2)]);
    }

    #[test]
    fn runs_task_to_completion() {
        let finished = Counter::default();

        let make_s = || {
            generate(|mut co| async {
                co.yield_(8u8).await;

                // Try really hard to cause this task to be dropped without completing.
                drop(co);
                yield_once().await;

                finished.inc();
            })
        };

        // No matter which combinator is used.

        block_on(async {
            let res = make_s().collect::<Vec<GeneratorState<u8, ()>>>().await;
            assert_eq!(res, vec![GeneratorState::Yielded(8), GeneratorState::Complete(())]);
            assert_eq!(finished.take(), 1);
        });

        block_on(async {
            assert_eq!(make_s().into_yielded().collect::<Vec<_>>().await, vec![8]);
            assert_eq!(finished.take(), 1);
        });

        block_on(async {
            let () = make_s().into_complete().await;
            assert_eq!(finished.take(), 1);
        });
    }

    #[test]
    fn fibonacci() {
        let fib = generate(|mut co| async move {
            let (mut a, mut b) = (0u32, 1u32);
            loop {
                co.yield_(a).await;

                let n = b;
                b = a + b;
                a = n;
            }
        })
        .into_yielded()
        .take(10)
        .collect::<Vec<_>>();

        assert_eq!(block_on(fib), vec![0, 1, 1, 2, 3, 5, 8, 13, 21, 34]);
    }
}
