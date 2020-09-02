// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    futures::{
        future::{Future, FutureExt, FutureObj},
        ready,
        task::{Context, Poll},
    },
    std::{marker::Unpin, pin::Pin},
    void::Void,
};

#[derive(Debug)]
pub struct ExitReason(pub Result<(), anyhow::Error>);

pub struct State<E>(FutureObj<'static, Result<State<E>, E>>);

pub struct StateMachine<E> {
    cur_state: State<E>,
}

impl<E> Unpin for StateMachine<E> {}

impl<E> Future for StateMachine<E> {
    type Output = Result<Void, E>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        loop {
            match ready!(self.cur_state.0.poll_unpin(cx)) {
                Ok(next) => self.cur_state = next,
                Err(e) => return Poll::Ready(Err(e)),
            }
        }
    }
}

pub trait IntoStateExt<E>: Future<Output = Result<State<E>, E>> {
    fn into_state(self) -> State<E>
    where
        Self: Sized + Send + 'static,
    {
        State(FutureObj::new(Box::new(self)))
    }

    fn into_state_machine(self) -> StateMachine<E>
    where
        Self: Sized + Send + 'static,
    {
        StateMachine { cur_state: self.into_state() }
    }
}

impl<F, E> IntoStateExt<E> for F where F: Future<Output = Result<State<E>, E>> {}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use futures::{channel::mpsc, stream::StreamExt};
    use std::mem;

    #[test]
    fn state_machine() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (sender, receiver) = mpsc::unbounded();
        let mut state_machine = sum_state(0, receiver).into_state_machine();

        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut state_machine));

        sender.unbounded_send(2).unwrap();
        sender.unbounded_send(3).unwrap();
        mem::drop(sender);

        assert_eq!(Poll::Ready(Err(5)), exec.run_until_stalled(&mut state_machine));
    }

    async fn sum_state(
        current: u32,
        stream: mpsc::UnboundedReceiver<u32>,
    ) -> Result<State<u32>, u32> {
        let (number, stream) = stream.into_future().await;
        match number {
            Some(number) => Ok(make_sum_state(current + number, stream)),
            None => Err(current),
        }
    }

    // A workaround for the "recursive impl Trait" problem in the compiler
    fn make_sum_state(current: u32, stream: mpsc::UnboundedReceiver<u32>) -> State<u32> {
        sum_state(current, stream).into_state()
    }
}
