// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Debug, PartialEq)]
pub struct StateMachine<S> {
    state: Option<S>,
}

impl<S> StateMachine<S> {
    pub fn new(state: S) -> Self {
        StateMachine { state: Some(state) }
    }

    pub fn replace_state<F>(&mut self, map: F)
    where
        F: FnOnce(S) -> S,
    {
        self.state = Some(map(self.state.take().expect("State can never be None")));
    }

    pub fn state(&self) -> &S {
        &self.state.as_ref().expect("State can never be None")
    }

    pub fn mut_state(&mut self) -> &mut S {
        self.state.as_mut().expect("State can never be None")
    }

    pub fn into_state(self) -> S {
        self.state.expect("State can never be None")
    }
}
