// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use futures::{future::Either, pin_mut, Future};

///! Utilities for tests

/// Run a background task while waiting for a future that should occur.
/// This is useful for running a task which you expect to produce side effects that
/// mean the task is operating correctly. i.e. reacting to a peer action by producing a
/// response on a client's hanging get.
/// `background_fut` is expected not to finish ans is returned to the caller, along with
/// the result of `result_fut`.  If `background_fut` finishes, this function will panic.
pub fn run_while<BackgroundFut, ResultFut, Out>(
    exec: &mut fasync::TestExecutor,
    background_fut: BackgroundFut,
    result_fut: ResultFut,
) -> (Out, BackgroundFut)
where
    BackgroundFut: Future + Unpin,
    ResultFut: Future<Output = Out>,
{
    pin_mut!(result_fut);
    match exec.run_singlethreaded(&mut futures::future::select(background_fut, result_fut)) {
        Either::Right(r) => r,
        Either::Left(_) => panic!("Background future finished"),
    }
}
