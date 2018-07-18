// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use futures::prelude::*;

pub struct State<E>(Box<Future<Item = State<E>, Error = E> + Send>);

impl<E> State<E> {
    pub fn into_future(self) -> StateMachine<E> {
        StateMachine{ cur_state: self }
    }
}

pub struct StateMachine<E>{
    cur_state: State<E>
}

impl<E> Future for StateMachine<E> {
    type Item = Never;
    type Error = E;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        loop {
            match self.cur_state.0.poll(cx) {
                Ok(Async::Ready(next)) => self.cur_state = next,
                Ok(Async::Pending) => return Ok(Async::Pending),
                Err(e) => return Err(e)
            }
        }
    }
}

pub trait IntoStateExt<E>: Future<Item = State<E>, Error = E> {
    fn into_state(self) -> State<E>
        where Self: Sized + Send + 'static
    {
        State(Box::new(self))
    }
}

impl<F, E> IntoStateExt<E> for F where F: Future<Item = State<E>, Error = E> {}

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
        assert_eq!(Ok(Async::Pending), r);

        sender.unbounded_send(2).unwrap();
        sender.unbounded_send(3).unwrap();
        mem::drop(sender);

        let r = exec.run_until_stalled(&mut state_machine);
        assert_eq!(Err(5), r);
    }

    fn sum_state(current: u32, stream: mpsc::UnboundedReceiver<u32>) -> State<u32> {
        stream.next()
            .map_err(|(e, _stream)| e.never_into())
            .and_then(move |(number, stream)| match number {
                Some(number) => Ok(sum_state(current + number, stream)),
                None => Err(current),
            })
            .into_state()
    }
}
