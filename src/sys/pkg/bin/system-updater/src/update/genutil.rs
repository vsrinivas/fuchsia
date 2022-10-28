// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_generator::{Generator, GeneratorState},
    futures::{
        prelude::*,
        ready,
        stream::FusedStream,
        task::{Context, Poll},
    },
    pin_project::pin_project,
    std::pin::Pin,
};

/// Extension utility combinators for [`async_generator::Generator`].
pub trait GeneratorExt<I, R>
where
    Self: Sized + Stream<Item = GeneratorState<I, R>>,
{
    /// Given a stream of `GeneratorState<I, R>`, returns a stream of `I` that, upon completion of
    /// the inner stream, invokes the provided async closure `f` with the last yielded `I` and the
    /// result of the generator task (`R`).
    ///
    /// The returned stream will never receive `R`, however, the returned stream won't terminate
    /// until the future returned by `f` completes.
    fn when_done<F, Fut>(self, f: F) -> WhenDone<Self, I, F, Fut>
    where
        I: Clone,
        F: FnOnce(Option<I>, R) -> Fut,
        Fut: Future<Output = ()>;
}

impl<T, I, R> GeneratorExt<I, R> for Generator<T, I, R>
where
    T: Future<Output = R>,
    I: Clone,
{
    fn when_done<F, Fut>(self, f: F) -> WhenDone<Self, I, F, Fut>
    where
        I: Clone,
        F: FnOnce(Option<I>, R) -> Fut,
        Fut: Future<Output = ()>,
    {
        WhenDone { stream: self, last_item: None, f: Some(f), pending: None }
    }
}

#[pin_project(project = WhenDoneProj)]
#[must_use = "streams do nothing unless polled"]
pub struct WhenDone<St, I, F, Fut> {
    #[pin]
    stream: St,
    last_item: Option<I>,
    f: Option<F>,
    #[pin]
    pending: Option<Fut>,
}

impl<St, I, R, F, Fut> FusedStream for WhenDone<St, I, F, Fut>
where
    St: Stream<Item = GeneratorState<I, R>>,
    I: Clone,
    F: FnOnce(Option<I>, R) -> Fut,
    Fut: Future<Output = ()>,
{
    fn is_terminated(&self) -> bool {
        self.f.is_none() && self.pending.is_none()
    }
}

impl<St, I, R, F, Fut> Stream for WhenDone<St, I, F, Fut>
where
    St: Stream<Item = GeneratorState<I, R>>,
    I: Clone,
    F: FnOnce(Option<I>, R) -> Fut,
    Fut: Future<Output = ()>,
{
    type Item = I;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.is_terminated() {
            return Poll::Ready(None);
        }

        let WhenDoneProj { mut stream, last_item, f, mut pending } = self.project();

        Poll::Ready(loop {
            // Check pending first, as if it is Some, the inner stream is already terminated.
            if let Some(p) = pending.as_mut().as_pin_mut() {
                let () = ready!(p.poll(cx));

                // The stream and final callback are all done.  Terminate this stream.
                pending.set(None);
                break None;
            } else if let Some(item) = ready!(stream.as_mut().poll_next(cx)) {
                match item {
                    GeneratorState::Yielded(item) => {
                        // The generator yielded a new item, save it in case it is the last one and
                        // yield it to this stream
                        *last_item = Some(item.clone());
                        break Some(item);
                    }
                    GeneratorState::Complete(result) => {
                        // The generator is done. Claim the callback and start running the future
                        // it returns. Note that the following panic on unwrap is unreachable.
                        let f = f.take().unwrap();
                        pending.set(Some((f)(last_item.take(), result)));
                    }
                }
            } else {
                // The inner stream terminated without yielding a GeneratorState::Complete.
                // Beyond the specific test case below, this should be unreachable, as
                // async_generator::Generator *always* yields a GeneratorState::Complete before
                // terminating. Drop the callback function to fuse this stream and terminate the
                // stream.
                f.take();
                break None;
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use {super::*, futures::channel::oneshot};
    #[allow(clippy::bool_assert_comparison)]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn when_done_handles_empty_generator() {
        let (send, recv) = oneshot::channel();

        let () = async_generator::generate(|_co| async move { true })
            .when_done(move |last_item, res| async move {
                assert_eq!(last_item, None);
                assert_eq!(res, true);
                send.send(42u32).unwrap();
            })
            .collect()
            .await;

        assert_eq!(recv.await.unwrap(), 42);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn when_done_provides_last_yielded_item() {
        let (send, recv) = oneshot::channel();

        assert_eq!(
            async_generator::generate(|mut co| async move {
                for i in 0..10 {
                    co.yield_(i).await;
                }
                42u32
            })
            .when_done(move |last_item, res| async move {
                assert_eq!(last_item, Some(9));
                assert_eq!(res, 42);
                send.send(res).unwrap();
            })
            .collect::<Vec<u32>>()
            .await,
            vec![0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
        );

        assert_eq!(recv.await.unwrap(), 42);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn when_done_fuses_to_none_after_handling_first_complete() {
        let evil_stream = stream::iter(vec![
            GeneratorState::Yielded(1u32),
            GeneratorState::Yielded(2u32),
            GeneratorState::Complete("all good so far"),
            // Outer stream never even polls the following:
            GeneratorState::Yielded(0u32),
            GeneratorState::Complete("illegal!"),
        ]);
        let f = move |last_item, res| {
            assert_eq!(last_item, Some(2));
            assert_eq!(res, "all good so far");
            future::ready(())
        };

        let stream = WhenDone { stream: evil_stream, last_item: None, f: Some(f), pending: None };
        futures::pin_mut!(stream);
        assert!(!stream.is_terminated());

        assert_eq!(stream.next().await, Some(1));
        assert!(!stream.is_terminated());

        assert_eq!(stream.next().await, Some(2));
        assert!(!stream.is_terminated());

        assert_eq!(stream.next().await, None);
        assert!(stream.is_terminated());

        assert_eq!(stream.next().await, None);
        assert!(stream.is_terminated());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn when_done_skips_closure_if_missing_complete() {
        let (send, recv) = oneshot::channel();
        futures::pin_mut!(recv);

        let evil_stream =
            stream::iter(vec![GeneratorState::Yielded(1u32), GeneratorState::Yielded(2u32)]);
        let f = move |_, _: &'static str| {
            send.send("unreachable").unwrap();
            future::ready(())
        };

        let stream = WhenDone { stream: evil_stream, last_item: None, f: Some(f), pending: None };
        futures::pin_mut!(stream);
        assert!(!stream.is_terminated());
        assert_eq!(futures::poll!(&mut recv), Poll::Pending);

        assert_eq!(stream.next().await, Some(1));
        assert!(!stream.is_terminated());
        assert_eq!(futures::poll!(&mut recv), Poll::Pending);

        assert_eq!(stream.next().await, Some(2));
        assert!(!stream.is_terminated());
        assert_eq!(futures::poll!(&mut recv), Poll::Pending);

        assert_eq!(stream.next().await, None);
        assert!(stream.is_terminated());
        assert_eq!(futures::poll!(recv), Poll::Ready(Err(oneshot::Canceled)));

        assert_eq!(stream.next().await, None);
        assert!(stream.is_terminated());
    }
}
