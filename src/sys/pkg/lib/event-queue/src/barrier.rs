// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    futures::{
        channel::mpsc,
        never::Never,
        task::{Context, Poll},
        Future, Stream,
    },
    std::pin::Pin,
};

/// A barrier allows an async task to wait for all blocks to be dropped.
#[derive(Debug)]
pub struct Barrier(mpsc::Receiver<Never>);

/// Any clone of a barrier block prevents the associated [`Barrier`] future from completing.
#[derive(Debug, Clone)]
pub struct BarrierBlock(mpsc::Sender<Never>);

impl Barrier {
    /// Creates a new barrier and associated blocker.
    ///
    /// The future that is [`Barrier`] resolves when all clones of [`BarrierBlock`] are dropped.
    pub fn new() -> (Self, BarrierBlock) {
        let (send, recv) = mpsc::channel(0);
        (Self(recv), BarrierBlock(send))
    }
}

impl Future for Barrier {
    type Output = ();

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let recv = Pin::new(&mut self.get_mut().0);
        recv.poll_next(cx).map(|_| ())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, futures::prelude::*};

    #[test]
    fn drop_single_block_unblocks_barrier() {
        let (barrier, _) = Barrier::new();
        assert_eq!(barrier.now_or_never(), Some(()));
    }

    #[test]
    fn single_block_blocks_barrier() {
        let mut executor = fuchsia_async::Executor::new().unwrap();

        let (mut barrier, block) = Barrier::new();
        assert_eq!(executor.run_until_stalled(&mut barrier), Poll::Pending);

        drop(block);
        assert_eq!(executor.run_until_stalled(&mut barrier), Poll::Ready(()));
    }

    #[test]
    fn block_clone_blocks_barrier() {
        let mut executor = fuchsia_async::Executor::new().unwrap();

        let (mut barrier, block) = Barrier::new();
        let block_clone = block.clone();
        assert_eq!(executor.run_until_stalled(&mut barrier), Poll::Pending);

        drop(block);
        assert_eq!(executor.run_until_stalled(&mut barrier), Poll::Pending);

        drop(block_clone);
        assert_eq!(executor.run_until_stalled(&mut barrier), Poll::Ready(()));
    }
}
