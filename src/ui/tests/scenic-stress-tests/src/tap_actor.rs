// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_ui_input as finput,
    std::convert::TryFrom,
    std::time::SystemTime,
    stress_test::actor::{Actor, ActorError},
};

pub struct TapActor {
    is_down: bool,
    device: finput::InputDeviceProxy,
}

impl TapActor {
    pub fn new(device: finput::InputDeviceProxy) -> Self {
        Self { is_down: false, device }
    }
}

#[async_trait]
impl Actor for TapActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        let touches = if self.is_down {
            // The actor's finger is touching the screen. Release it
            self.is_down = false;
            vec![]
        } else {
            // The actor's finger is not touching the screen. Touch it.
            self.is_down = true;
            vec![finput::Touch { finger_id: 0, x: 320, y: 240, width: 0, height: 0 }]
        };

        // Record the current time as the event time in nanoseconds
        let now = SystemTime::now();
        let now = now.duration_since(SystemTime::UNIX_EPOCH).unwrap();
        let now = now.as_nanos();
        let now = u64::try_from(now).unwrap();

        let mut report = finput::InputReport {
            event_time: now,
            keyboard: None,
            media_buttons: None,
            mouse: None,
            stylus: None,
            sensor: None,
            touchscreen: Some(Box::new(finput::TouchscreenReport { touches })),
            trace_id: 0,
        };

        // Send the touch event to scenic
        self.device.dispatch_report(&mut report).expect("Could not dispatch touch report");

        Ok(())
    }
}
