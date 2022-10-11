// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    appkit::{Event, EventSender, Window, WindowEvent, WindowId},
    fidl_fuchsia_input::Key,
    fidl_fuchsia_math as fmath, fidl_fuchsia_ui_composition as ui_comp,
    fidl_fuchsia_ui_input3::{KeyEvent, KeyEventStatus, KeyEventType},
    futures::StreamExt,
    std::collections::HashMap,
    std::convert::TryInto,
    tracing::*,
};

const BOX_ID: ui_comp::ContentId = ui_comp::ContentId { value: 2 };

struct Bouncer {
    flatland: ui_comp::FlatlandProxy,
    transform_id: ui_comp::TransformId,
    size: fidl_fuchsia_math::SizeU,
    pos: fidl_fuchsia_math::Vec_,
    velocity: fidl_fuchsia_math::Vec_,
}

impl Bouncer {
    pub fn new(flatland: ui_comp::FlatlandProxy, mut transform_id: ui_comp::TransformId) -> Self {
        flatland.create_filled_rect(&mut BOX_ID.clone()).expect("fidl error");
        flatland
            .set_solid_fill(
                &mut BOX_ID.clone(),
                &mut ui_comp::ColorRgba { red: 1.0, green: 0.0, blue: 0.0, alpha: 1.0 },
                &mut fidl_fuchsia_math::SizeU { width: 100, height: 100 },
            )
            .expect("fidl error");
        flatland.set_content(&mut transform_id, &mut BOX_ID.clone()).expect("fidl error");

        Bouncer {
            flatland: flatland.clone(),
            transform_id: transform_id.clone(),
            size: fmath::SizeU { width: 0, height: 0 },
            pos: fmath::Vec_ { x: 0, y: 0 },
            velocity: fmath::Vec_ { x: 5, y: 10 },
        }
    }

    /// Move the object, potentially bouncing off the sides.
    fn update(&mut self) {
        let width: i32 = self.size.width.try_into().unwrap();
        let height: i32 = self.size.height.try_into().unwrap();
        if self.pos.x < 0 || width - 100 < self.pos.x {
            self.pos.x = self.pos.x.clamp(0, width - 100);
            self.velocity.x = -self.velocity.x;
        }
        if self.pos.y < 0 || height - 100 < self.pos.y {
            self.pos.y = self.pos.y.clamp(0, height - 100);
            self.velocity.y = -self.velocity.y;
        }
        self.pos.x += self.velocity.x;
        self.pos.y += self.velocity.y;

        self.flatland
            .set_translation(&mut self.transform_id.clone(), &mut self.pos)
            .expect("fidl error");
    }
}

#[derive(Debug)]
enum BouncerEvent {}

struct App<T> {
    bouncer: Option<Bouncer>,
    event_sender: EventSender<T>,
    windows: HashMap<WindowId, Window<T>>,
}

impl<T> App<T> {
    fn handle_event(&mut self, event: Event<T>) -> Result<(), Error>
    where
        T: 'static + Sync + Send,
    {
        match event {
            Event::Init => {
                // Create the application's window.
                let mut window =
                    Window::new(self.event_sender.clone()).with_title("Bouncing Box".to_owned());
                window.create_view()?;

                // Create the bouncer.
                let flatland = window.get_flatland();
                let root_transform_id = window.get_root_transform_id();
                self.bouncer = Some(Bouncer::new(flatland, root_transform_id));

                self.windows.insert(window.id(), window);
            }
            Event::WindowEvent { window_id: id, event: window_event } => {
                let window = self.windows.get_mut(&id).unwrap();
                match window_event {
                    WindowEvent::Resized { width, height } => {
                        // Set the bouncer's size.
                        let size = fmath::SizeU { width, height };
                        self.bouncer.as_mut().map(|bouncer| bouncer.size = size);
                    }
                    WindowEvent::NeedsRedraw { .. } => {
                        // Update bouncer's position on every frame.
                        self.bouncer.as_mut().map(|bouncer| bouncer.update());
                        window.redraw();
                    }
                    WindowEvent::Keyboard { event, responder } => {
                        // Quit app on 'q' is pressed.
                        if let KeyEvent {
                            type_: Some(KeyEventType::Pressed),
                            key: Some(Key::Q),
                            ..
                        } = event
                        {
                            window.close()?;
                            responder.send(KeyEventStatus::Handled)?;
                        } else {
                            responder.send(KeyEventStatus::NotHandled)?;
                        }
                    }
                    WindowEvent::Closed => {
                        self.event_sender
                            .send(Event::Exit)
                            .expect("Failed to send Event::Exit event");
                    }
                    _ => {}
                }
            }
            _ => {}
        }
        Ok(())
    }
}

#[fuchsia::main(logging = true)]
async fn main() -> Result<(), Error> {
    info!("Started...");
    let (event_sender, mut receiver) = EventSender::<BouncerEvent>::new();
    let mut app = App::<BouncerEvent> {
        bouncer: None,
        event_sender: event_sender.clone(),
        windows: HashMap::new(),
    };

    while let Some(event) = receiver.next().await {
        if matches!(event, Event::Exit) {
            receiver.close();
        }
        app.handle_event(event).expect("Failed to handle event");
    }

    info!("Stopped");
    Ok(())
}
