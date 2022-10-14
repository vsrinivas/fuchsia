// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, fidl_fuchsia_ui_pointer as fptr};

impl PointerFusionState {
    pub(super) fn fuse_touch(&mut self, event: fptr::TouchEvent) -> Vec<PointerEvent> {
        // A valid TouchEvent should have interaction, phase and position_in_viewport for us to
        // fuse it into a PointerEvent.
        if let Some(fptr::TouchPointerSample {
            interaction: Some(interaction),
            phase: Some(phase),
            position_in_viewport: Some(position_in_viewport),
            ..
        }) = event.pointer_sample
        {
            if event.view_parameters.is_some() {
                self.view_parameters = event.view_parameters;
            }

            if self.view_parameters.is_some() {
                let mut pointer_event = PointerEvent {
                    timestamp: zx::Time::from_nanos(event.timestamp.unwrap_or(0)),
                    phase: compute_touch_phase(phase),
                    kind: DeviceKind::Touch,
                    pointer_id: Some(interaction.pointer_id),
                    device_id: interaction.device_id,
                    ..PointerEvent::default()
                };

                let [logical_x, logical_y] = viewport_to_view_coordinates(
                    position_in_viewport,
                    &self.view_parameters.unwrap(),
                );
                pointer_event.physical_x = logical_x * self.pixel_ratio;
                pointer_event.physical_y = logical_y * self.pixel_ratio;

                // Synthesize a Down event after Add and an Up event before Remove to keep the event
                // stream sensible for clients.
                let mut events = vec![];
                match phase {
                    fptr::EventPhase::Add => {
                        let mut down_event = pointer_event.clone();
                        down_event.phase = Phase::Down;
                        down_event.synthesized = true;
                        events.append(&mut self.sanitize_pointer(pointer_event));
                        events.append(&mut self.sanitize_pointer(down_event));
                    }
                    fptr::EventPhase::Remove => {
                        let mut up_event = pointer_event.clone();
                        up_event.phase = Phase::Up;
                        up_event.synthesized = true;
                        events.append(&mut self.sanitize_pointer(up_event));
                        events.append(&mut self.sanitize_pointer(pointer_event));
                    }
                    _ => events.push(pointer_event),
                }

                return events;
            }
        }
        vec![]
    }
}

fn compute_touch_phase(phase: fptr::EventPhase) -> Phase {
    match phase {
        fptr::EventPhase::Add => Phase::Add,
        fptr::EventPhase::Change => Phase::Move,
        fptr::EventPhase::Remove => Phase::Remove,
        _ => Phase::Cancel,
    }
}
