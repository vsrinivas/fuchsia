// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fidl_fuchsia_ui_pointer as fptr,
    // fidl_fuchsia_input_report as input_report,
    futures::FutureExt,
};

#[fuchsia::test]
async fn test_mouse_event_without_view_parameters() {
    // Fusing mouse event without [fptr::ViewParameters] should not result in a PointerEvent.
    let mouse_event = InputEvent::mouse();
    let (sender, mut receiver) = pointer_fusion(1.0);
    sender.unbounded_send(mouse_event).unwrap();
    let pointer_event = receiver.next().now_or_never();
    assert!(pointer_event.is_none(), "Received {:?}", pointer_event);
}

#[fuchsia::test]
async fn test_mouse_event_without_mouse_sample() {
    // Fusing mouse event without [fptr::MousePointerSample] should not result in a PointerEvent.
    let mouse_event = InputEvent::mouse().view(1024.0, 600.0);
    let (sender, mut receiver) = pointer_fusion(1.0);
    sender.unbounded_send(mouse_event).unwrap();
    let pointer_event = receiver.next().now_or_never();
    assert!(pointer_event.is_none(), "Received {:?}", pointer_event);
}

#[fuchsia::test]
async fn test_mouse_event_without_device_info() {
    // Fusing mouse event without [fptr::MouseDeviceInfo] should not result in a PointerEvent.
    let mouse_event = InputEvent::mouse().view(1024.0, 600.0).position(512.0, 300.0);
    let (sender, mut receiver) = pointer_fusion(1.0);
    sender.unbounded_send(mouse_event).unwrap();
    let pointer_event = receiver.next().now_or_never();
    assert!(pointer_event.is_none(), "Received {:?}", pointer_event);
}

#[fuchsia::test]
async fn test_pixel_ratio() {
    let mouse_event = InputEvent::mouse()
        .view(1024.0, 600.0)
        .device_info(42)
        .position(512.0, 300.0)
        .button_down();

    let (sender, mut receiver) = pointer_fusion(2.0);
    sender.unbounded_send(mouse_event).unwrap();

    let pointer_event = receiver.next().await.unwrap();
    assert!(matches!(pointer_event.phase, Phase::Add));
    assert!(pointer_event.physical_x - 1024.0 < std::f32::EPSILON);
    assert!(pointer_event.physical_y - 600.0 < std::f32::EPSILON);
}

#[fuchsia::test]
async fn test_mouse_starts_with_add_event() {
    let mouse_event =
        InputEvent::mouse().view(1024.0, 600.0).device_info(42).position(512.0, 300.0);

    let (sender, mut receiver) = pointer_fusion(1.0);
    sender.unbounded_send(mouse_event).unwrap();
    let pointer_event = receiver.next().await.unwrap();
    assert!(matches!(pointer_event.phase, Phase::Add));
}

#[fuchsia::test]
async fn test_mouse_tap() {
    let mouse_event = InputEvent::mouse()
        .view(1024.0, 600.0)
        .device_info(42)
        .position(512.0, 300.0)
        .button_down();

    let (sender, mut receiver) = pointer_fusion(1.0);
    sender.unbounded_send(mouse_event).unwrap();

    let pointer_event = receiver.next().await.unwrap();
    assert!(matches!(pointer_event.phase, Phase::Add));

    let pointer_event = receiver.next().await.unwrap();
    assert!(matches!(pointer_event.phase, Phase::Down));

    let mouse_event = InputEvent::mouse().device_info(42).position(512.0, 300.0);
    sender.unbounded_send(mouse_event).unwrap();

    let pointer_event = receiver.next().await.unwrap();
    assert!(matches!(pointer_event.phase, Phase::Up));
}

#[fuchsia::test]
async fn test_mouse_hover() {
    let mouse_event =
        InputEvent::mouse().view(1024.0, 600.0).device_info(42).position(512.0, 300.0);

    let (sender, mut receiver) = pointer_fusion(1.0);
    sender.unbounded_send(mouse_event).unwrap();

    let pointer_event = receiver.next().await.unwrap();
    assert!(matches!(pointer_event.phase, Phase::Add));

    // Changing mouse x position should result in Hover event.
    let mouse_event = InputEvent::mouse().device_info(42).position(540.0, 300.0);
    sender.unbounded_send(mouse_event).unwrap();

    let pointer_event = receiver.next().await.unwrap();
    assert!(matches!(pointer_event.phase, Phase::Hover));
    assert!(pointer_event.physical_x - 540.0 < std::f32::EPSILON);

    // Changing mouse y position should result in Hover event.
    let mouse_event = InputEvent::mouse().device_info(42).position(540.0, 320.0);
    sender.unbounded_send(mouse_event).unwrap();

    let pointer_event = receiver.next().await.unwrap();
    assert!(matches!(pointer_event.phase, Phase::Hover));
    assert!(pointer_event.physical_y - 320.0 < std::f32::EPSILON);
}

#[fuchsia::test]
async fn test_mouse_move() {
    let mouse_event = InputEvent::mouse()
        .view(1024.0, 600.0)
        .device_info(42)
        .position(512.0, 300.0)
        .button_down();

    let (sender, mut receiver) = pointer_fusion(1.0);
    sender.unbounded_send(mouse_event).unwrap();

    let pointer_event = receiver.next().await.unwrap();
    assert!(matches!(pointer_event.phase, Phase::Add));

    let pointer_event = receiver.next().await.unwrap();
    assert!(matches!(pointer_event.phase, Phase::Down));

    // Changing the position should result in Move event.
    let mouse_event = InputEvent::mouse().device_info(42).position(540.0, 320.0).button_down();
    sender.unbounded_send(mouse_event).unwrap();

    let pointer_event = receiver.next().await.unwrap();
    assert!(matches!(pointer_event.phase, Phase::Move));
    assert!(pointer_event.physical_delta_x == 28.0);
    assert!(pointer_event.physical_delta_y == 20.0);

    // Keeping the same position should not result in Move event.
    let mouse_event = InputEvent::mouse().device_info(42).position(540.0, 320.0).button_down();
    sender.unbounded_send(mouse_event).unwrap();

    let pointer_event = receiver.next().now_or_never();
    assert!(pointer_event.is_none(), "Received {:?}", pointer_event);
}

#[fuchsia::test]
async fn test_mouse_no_spurious_hovers() {
    let mouse_event =
        InputEvent::mouse().view(1024.0, 600.0).device_info(42).position(512.0, 300.0);

    let (sender, mut receiver) = pointer_fusion(1.0);
    sender.unbounded_send(mouse_event).unwrap();

    let pointer_event = receiver.next().await.unwrap();
    assert!(matches!(pointer_event.phase, Phase::Add));

    // Same position should not result in any hover event.
    let mouse_event = InputEvent::mouse().device_info(42).position(512.0, 300.0);
    sender.unbounded_send(mouse_event).unwrap();

    let pointer_event = receiver.next().now_or_never();
    assert!(pointer_event.is_none(), "Received {:?}", pointer_event);
}

trait TestMouseEvent {
    fn mouse() -> Self;
    fn view(self, width: f32, height: f32) -> Self;
    fn device_info(self, id: u32) -> Self;
    fn position(self, x: f32, y: f32) -> Self;
    fn button_down(self) -> Self;
}

impl TestMouseEvent for InputEvent {
    fn mouse() -> Self {
        InputEvent::MouseEvent(fptr::MouseEvent { ..fptr::MouseEvent::EMPTY })
    }

    fn view(mut self, width: f32, height: f32) -> Self {
        match self {
            InputEvent::MouseEvent(ref mut event) => {
                event.view_parameters = Some(fptr::ViewParameters {
                    view: fptr::Rectangle { min: [0.0, 0.0], max: [width, height] },
                    viewport: fptr::Rectangle { min: [0.0, 0.0], max: [width, height] },
                    viewport_to_view_transform: [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0],
                });
            }
            _ => {}
        }
        self
    }

    fn device_info(mut self, id: u32) -> Self {
        match self {
            InputEvent::MouseEvent(ref mut event) => {
                event.device_info = Some(fptr::MouseDeviceInfo {
                    id: Some(id),
                    buttons: Some([0, 1, 2].to_vec()),
                    relative_motion_range: None,
                    ..fptr::MouseDeviceInfo::EMPTY
                });
            }
            _ => {}
        }
        self
    }

    fn position(mut self, x: f32, y: f32) -> Self {
        match self {
            InputEvent::MouseEvent(ref mut event) => {
                let device_id = event
                    .device_info
                    .as_ref()
                    .unwrap_or(&fptr::MouseDeviceInfo {
                        id: Some(0),
                        ..fptr::MouseDeviceInfo::EMPTY
                    })
                    .id;
                event.pointer_sample = Some(fptr::MousePointerSample {
                    device_id,
                    position_in_viewport: Some([x, y]),
                    ..fptr::MousePointerSample::EMPTY
                });
            }
            _ => {}
        }
        self
    }

    fn button_down(mut self) -> Self {
        match self {
            InputEvent::MouseEvent(ref mut event) => {
                if let Some(ref mut pointer_sample) = event.pointer_sample {
                    pointer_sample.pressed_buttons = Some(vec![0]);
                }
            }
            _ => {}
        }
        self
    }
}
