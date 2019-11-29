// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::state_machine::State;
use futures::future::LocalBoxFuture;
use std::fmt;

/// Observe changes in the state machine.
pub trait Observer {
    fn on_state_change(&mut self, _state: State) -> LocalBoxFuture<'_, ()>;
}

impl fmt::Debug for dyn Observer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Observer")
    }
}
