// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_async as fasync, futures::future::Future, std::cell::RefCell, std::ops::DerefMut};

thread_local!(static GLOBAL_EXECUTOR: RefCell<Option<fasync::Executor>> = RefCell::new(None));

pub fn run_singlethreaded<F>(fut: F) -> F::Output
where
    F: Future,
{
    GLOBAL_EXECUTOR.with(|rc| {
        let mut borrow = rc.borrow_mut();
        match borrow.deref_mut() {
            None => panic!("no global executor"),
            Some(executor) => executor.run_singlethreaded(fut),
        }
    })
}

pub fn with<F, O>(executor: fasync::Executor, func: F) -> O
where
    F: Fn() -> O,
{
    GLOBAL_EXECUTOR.with(|rc| {
        if rc.replace(Some(executor)).is_some() {
            panic!("global executor was already set");
        }
    });

    let o = func();

    GLOBAL_EXECUTOR.with(|rc| {
        rc.replace(None);
    });

    o
}
