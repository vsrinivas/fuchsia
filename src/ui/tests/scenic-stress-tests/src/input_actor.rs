// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::pointer_state::PointerState,
    async_trait::async_trait,
    fidl_fuchsia_ui_pointerinjector as pointerinjector,
    stress_test::actor::{Actor, ActorError},
};

pub struct InputActor {
    injector: pointerinjector::DeviceProxy,
    state: PointerState,
}

impl InputActor {
    pub fn new(
        injector: pointerinjector::DeviceProxy,
        display_width: u16,
        display_height: u16,
    ) -> Self {
        let state = PointerState::new(display_width, display_height);
        Self { injector, state }
    }
}

#[async_trait]
impl Actor for InputActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        let event = self.state.next_event();
        let fut = self.injector.inject(&mut std::iter::once(event));
        fut.await.expect("Injection failed");
        Ok(())
    }
}
