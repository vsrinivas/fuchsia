// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async::Interval;
use futures::{prelude::*, future};
use zx;

pub fn retry_until<T, E, FUNC, FUT>(retry_interval: zx::Duration, mut f: FUNC)
    -> impl Future<Item = T, Error = E>
    where FUNC: FnMut() -> FUT,
          FUT: Future<Item = Option<T>, Error = E>
{
    let fut = f();
    fut.and_then(move |maybe_item| match maybe_item {
        Some(item) => future::ok(item).left_future(),
        None => Interval::new(retry_interval)
                    .and_then(move |()| f())
                    .filter_map(|x| Ok(x))
                    .next()
                    .map_err(|(e, _stream)| e)
                    .map(|(x, _stream)| {
                        x.expect("Interval stream is not expected to ever end")
                    })
                    .right_future()
    })
}
