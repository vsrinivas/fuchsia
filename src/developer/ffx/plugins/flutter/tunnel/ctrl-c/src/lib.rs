// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    atomic_waker::AtomicWaker,
    futures::future::Future,
    futures::never::Never,
    futures::task::{Context, Poll},
    std::pin::Pin,
    std::sync::atomic::{AtomicBool, Ordering},
    std::sync::Arc,
};

struct CtrlCInner {
    waker: AtomicWaker,
    term: Arc<AtomicBool>,
}

#[derive(Clone)]
struct CtrlC(Arc<CtrlCInner>);

impl CtrlC {
    pub fn new() -> Self {
        CtrlC(Arc::new(CtrlCInner {
            waker: AtomicWaker::new(),
            term: Arc::new(AtomicBool::new(false)),
        }))
    }

    fn signal(&self) {
        self.0.term.store(true, Ordering::Relaxed);
        self.0.waker.wake();
    }
}

impl Future for CtrlC {
    type Output = ();

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        if self.0.term.load(Ordering::Relaxed) {
            return Poll::Ready(());
        }

        self.0.waker.register(cx.waker());

        if self.0.term.load(Ordering::Relaxed) {
            Poll::Ready(())
        } else {
            Poll::Pending
        }
    }
}

pub async fn wait_for_kill() -> Result<(), Never> {
    let ctrlc = CtrlC::new();
    let _ = unsafe {
        let ctrlc_clone = ctrlc.clone();
        signal_hook::low_level::register(signal_hook::consts::SIGINT, move || ctrlc_clone.signal())
    };
    println!("Press Ctrl-C to kill ssh connection . . .");
    Ok(ctrlc.await)
}
