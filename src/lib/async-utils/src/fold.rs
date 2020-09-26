// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides utilities to fold [`Stream`]s and [`TryStream`]s with a
//! short-circuited result.

use futures::{Future, FutureExt, Stream, StreamExt, TryStream, TryStreamExt};

/// Controls folding behavior.
#[derive(Debug)]
pub enum FoldWhile<C, D> {
    /// Continue folding with state `C`.
    Continue(C),
    /// Short-circuit folding with result `D`.
    Done(D),
}

/// The result of folding a stream.
#[derive(Debug, Eq, PartialEq, Clone, Copy)]
pub enum FoldResult<F, R> {
    /// The stream ended with folded state `F`.
    StreamEnded(F),
    /// The stream was short-cirtuited with result `R`.
    ShortCircuited(R),
}

impl<F, R> FoldResult<F, R> {
    /// Transforms into [`Result`] mapping the [`FoldResult::StreamEnded`]
    /// variant into `Ok`.
    pub fn ended(self) -> Result<F, R> {
        match self {
            FoldResult::StreamEnded(r) => Ok(r),
            FoldResult::ShortCircuited(r) => Err(r),
        }
    }

    /// Transforms into [`Result`] mapping the [`FoldResult::ShortCircuited`]
    /// variant into `Ok`.
    pub fn short_circuited(self) -> Result<R, F> {
        match self {
            FoldResult::StreamEnded(r) => Err(r),
            FoldResult::ShortCircuited(r) => Ok(r),
        }
    }
}

impl<F> FoldResult<F, F> {
    /// Unwraps this [`FoldResult`] into its inner value, discarding the variant
    /// information.
    pub fn into_inner(self) -> F {
        match self {
            FoldResult::StreamEnded(r) | FoldResult::ShortCircuited(r) => r,
        }
    }
}

/// Similar to [`TryStreamExt::try_fold`], but the closure `f` can short-circuit
/// the operation by returning [`FoldWhile::Done`].
///
/// Returns [`FoldResult::StreamEnded`] with the current folded value when the
/// stream ends. Returns [`FoldResult::ShortCircuited`] with the value of
/// [`FoldWhile::Done`] if `f` short-circuits the operation.
/// Returns `Err` if either `s` or `f` returns an error.
pub fn try_fold_while<S, T, D, F, Fut>(
    s: S,
    init: T,
    mut f: F,
) -> impl Future<Output = Result<FoldResult<T, D>, S::Error>>
where
    S: TryStream,
    F: FnMut(T, S::Ok) -> Fut,
    Fut: Future<Output = Result<FoldWhile<T, D>, S::Error>>,
{
    s.map_err(Err)
        .try_fold(init, move |acc, n| {
            f(acc, n).map(|r| match r {
                Ok(FoldWhile::Continue(r)) => Ok(r),
                Ok(FoldWhile::Done(d)) => Err(Ok(d)),
                Err(e) => Err(Err(e)),
            })
        })
        .map(|r| match r {
            Ok(n) => Ok(FoldResult::StreamEnded(n)),
            Err(Ok(n)) => Ok(FoldResult::ShortCircuited(n)),
            Err(Err(e)) => Err(e),
        })
}

/// Similar to [`StreamExt::fold`], but the closure `f` can short-circuit
/// the operation by returning [`FoldWhile::Done`].
///
/// Returns [`FoldResult::StreamEnded`] with the current folded value when the
/// stream ends. Returns [`FoldResult::ShortCircuited`] with the value of
/// [`FoldWhile::Done`] if `f` short-circuits the operation.
pub fn fold_while<S, T, D, F, Fut>(
    s: S,
    init: T,
    mut f: F,
) -> impl Future<Output = FoldResult<T, D>>
where
    S: Stream,
    F: FnMut(T, S::Item) -> Fut,
    Fut: Future<Output = FoldWhile<T, D>>,
{
    s.map(Ok)
        .try_fold(init, move |acc, n| {
            f(acc, n).map(|r| match r {
                FoldWhile::Continue(r) => Ok(r),
                FoldWhile::Done(d) => Err(d),
            })
        })
        .map(|r| match r {
            Ok(n) => FoldResult::StreamEnded(n),
            Err(n) => FoldResult::ShortCircuited(n),
        })
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use futures::channel::mpsc;
    use futures::future;

    #[fasync::run_singlethreaded(test)]
    async fn test_try_fold_while_short_circuit() {
        let (sender, stream) = mpsc::unbounded::<u32>();
        const STOP_AT: u32 = 5;
        let mut sum = 0;
        for i in 0..10 {
            if i < STOP_AT {
                sum += i;
            }
            let () = sender.unbounded_send(i).expect("failed to send item");
        }
        let (acc, stop) = try_fold_while(stream.map(Result::<_, ()>::Ok), 0, |acc, next| {
            future::ok(if next == STOP_AT {
                FoldWhile::Done((acc, next))
            } else {
                FoldWhile::Continue(next + acc)
            })
        })
        .await
        .expect("try_fold_while failed")
        .short_circuited()
        .expect("try_fold_while should've short-circuited");
        assert_eq!(stop, STOP_AT);
        assert_eq!(acc, sum);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_fold_while_stream_ended() {
        let (sender, stream) = mpsc::unbounded::<u32>();
        let mut sum = 0u32;
        for i in 0..10 {
            sum += i;
            let () = sender.unbounded_send(i).expect("failed to send item");
        }
        std::mem::drop(sender);
        let result =
            try_fold_while::<_, _, (), _, _>(stream.map(Result::<_, ()>::Ok), 0, |acc, next| {
                future::ok(FoldWhile::Continue(next + acc))
            })
            .await
            .expect("try_fold_while failed")
            .ended()
            .expect("try_fold_while should have seen the stream end");

        assert_eq!(result, sum);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_fold_while_stream_error() {
        #[derive(Debug)]
        struct StreamErr;
        let (sender, stream) = mpsc::unbounded::<Result<u32, StreamErr>>();
        let () = sender.unbounded_send(Err(StreamErr {})).expect("failed to send item");
        let StreamErr {} = try_fold_while::<_, _, (), _, _>(stream, (), |(), _: u32| async {
            panic!("shouldn't receive error input");
        })
        .await
        .expect_err("try_fold_while should return error");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_fold_while_closure_error() {
        #[derive(Debug)]
        struct StreamErr {
            item: u32,
        };
        const ERROR_ITEM: u32 = 1234;
        let (sender, stream) = mpsc::unbounded::<Result<u32, StreamErr>>();
        let () = sender.unbounded_send(Ok(ERROR_ITEM)).expect("failed to send item");
        let StreamErr { item } = try_fold_while::<_, _, (), _, _>(stream, (), |(), item| {
            future::err(StreamErr { item })
        })
        .await
        .expect_err("try_fold_while should return error");
        assert_eq!(item, ERROR_ITEM);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fold_while_short_circuit() {
        let (sender, stream) = mpsc::unbounded::<u32>();
        const STOP_AT: u32 = 5;
        let mut sum = 0;
        for i in 0..10 {
            if i < STOP_AT {
                sum += i;
            }
            let () = sender.unbounded_send(i).expect("failed to send item");
        }
        let (acc, stop) = fold_while(stream, 0, |acc, next| {
            future::ready(if next == STOP_AT {
                FoldWhile::Done((acc, next))
            } else {
                FoldWhile::Continue(next + acc)
            })
        })
        .await
        .short_circuited()
        .expect("fold_while should've short-circuited");
        assert_eq!(stop, STOP_AT);
        assert_eq!(acc, sum);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fold_while_stream_ended() {
        let (sender, stream) = mpsc::unbounded::<u32>();
        let mut sum = 0u32;
        for i in 0..10 {
            sum += i;
            let () = sender.unbounded_send(i).expect("failed to send item");
        }
        std::mem::drop(sender);
        let result = fold_while::<_, _, (), _, _>(stream, 0, |acc, next| {
            future::ready(FoldWhile::Continue(next + acc))
        })
        .await
        .ended()
        .expect("fold_while should have seen the stream end");

        assert_eq!(result, sum);
    }

    #[test]
    fn test_fold_result_into_inner() {
        let x = FoldResult::<u32, u32>::StreamEnded(1);
        let y = FoldResult::<u32, u32>::ShortCircuited(2);
        assert_eq!(x.into_inner(), 1);
        assert_eq!(y.into_inner(), 2);
    }

    #[test]
    fn test_fold_result_mapping() {
        type FoldResult = super::FoldResult<u32, bool>;
        assert_eq!(FoldResult::StreamEnded(1).ended(), Ok(1));
        assert_eq!(FoldResult::ShortCircuited(false).ended(), Err(false));

        assert_eq!(FoldResult::StreamEnded(2).short_circuited(), Err(2));
        assert_eq!(FoldResult::ShortCircuited(true).short_circuited(), Ok(true));
    }
}
