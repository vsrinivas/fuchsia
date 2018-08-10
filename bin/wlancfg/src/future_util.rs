// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async::{Interval, temp::{TempFutureExt, TempStreamExt}};
use futures::{prelude::*, future};
use zx;

pub fn retry_until<T, E, FUNC, FUT>(retry_interval: zx::Duration, mut f: FUNC)
    -> impl Future<Output = Result<T, E>>
    where FUNC: FnMut() -> FUT,
          FUT: Future<Output = Result<Option<T>, E>>
{
    let fut = f();
    fut.and_then(move |maybe_item| match maybe_item {
        Some(item) => future::ready(Ok(item)).left_future(),
        None => Interval::new(retry_interval)
                    .then(move |()| f())
                    .filter_map(|x| future::ready(x.transpose()))
                    .first_elem()
                    .map(|x| x.expect("Interval stream is not expected to ever end"))
                    .right_future()
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use async;
    use Never;
    use zx::prelude::*;

    #[test]
    fn first_try() {
        let mut exec = async::Executor::new().expect("failed to create an executor");
        let mut fut = retry_until::<_, Never, _, _>(5.seconds(), || future::ready(Ok(Some(123))));
        assert_eq!(Poll::Ready(Ok(123)), exec.run_until_stalled(&mut fut));
    }

    #[test]
    fn third_try() {
        let mut exec = async::Executor::new().expect("failed to create an executor");
        let mut countdown = 3;
        let start = 0.seconds().after_now();
        let mut fut = retry_until::<_, Never, _, _>(
            5.seconds(),
            move || {
                countdown -= 1;
                if countdown == 0 {
                    future::ready(Ok(Some(123)))
                } else {
                    future::ready(Ok(None))
                }
            });
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        let first_timeout = exec.wake_next_timer().expect("expected a pending timer");
        assert!(first_timeout >= start + 5.seconds());
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        let second_timeout = exec.wake_next_timer().expect("expected a pending timer");
        assert_eq!(first_timeout + 5.seconds(), second_timeout);
        assert_eq!(Poll::Ready(Ok(123)), exec.run_until_stalled(&mut fut));
    }
}
