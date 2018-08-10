// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use Never;
use futures::future::FutureObj;
use futures::prelude::*;
use std::marker::Unpin;
use std::mem::PinMut;

pub struct State<E>(FutureObj<'static, Result<State<E>, E>>);

impl<E> State<E> {
    pub fn into_future(self) -> StateMachine<E> {
        StateMachine{ cur_state: self }
    }
}

pub struct StateMachine<E>{
    cur_state: State<E>
}

impl<E> Unpin for StateMachine<E> {}

impl<E> Future for StateMachine<E> {
    type Output = Result<Never, E>;

    fn poll(mut self: PinMut<Self>, cx: &mut task::Context) -> Poll<Self::Output> {
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
        where Self: Sized + Send + 'static
    {
        State(FutureObj::new(Box::new(self)))
    }
}

impl<F, E> IntoStateExt<E> for F where F: Future<Output = Result<State<E>, E>> {}

#[cfg(test)]
mod tests {
    use super::*;
    use async;
    use futures::channel::mpsc;
    use std::mem;

    #[test]
    fn state_machine() {
        let mut exec = async::Executor::new().expect("Failed to create an executor");
        let (sender, receiver) = mpsc::unbounded();
        let mut state_machine = sum_state(0, receiver).into_future();

        let r = exec.run_until_stalled(&mut state_machine);
        assert_eq!(Poll::Pending, r);

        sender.unbounded_send(2).unwrap();
        sender.unbounded_send(3).unwrap();
        mem::drop(sender);

        let r = exec.run_until_stalled(&mut state_machine);
        assert_eq!(Poll::Ready(Err(5)), r);
    }

    fn sum_state(current: u32, stream: mpsc::UnboundedReceiver<u32>) -> State<u32> {
        stream.into_future()
            .map(move |(number, stream)| match number {
                Some(number) => Ok(sum_state(current + number, stream)),
                None => Err(current),
            })
            .into_state()
    }
}
