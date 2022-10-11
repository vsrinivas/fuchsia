
# AppKit

A Rust library for building multi window applications for Fuchsia.

## Features

- Support creating multipe windows with GraphicalPresenter.
- Support input types: keyboard, mouse and touch.
- Support for keyboard shortcuts.
- Support for child view embedding.
- Focus management for child views.

## Programming model

The AppKit library offers a simple event loop based programming model:

```
    let event_handler = |event| {
        match event {
            Event::Init => {
                let mut window = WindowBuilder::new()
                    .with_title("Bouncing Box".to_owned())
                    .build(self.event_sender.clone())
                    .unwrap();
                window.create_view()?;
            }
            Event::WindowEvent(id, window_event) => {
                let window = self.windows.get_mut(&id).unwrap();
                match window_event {
                    WindowEvent::NeedsRedraw(_presentation_time) => {
                        // Drawing code here.
                        window.redraw()?;
                    }
                    WindowEvent::Keyboard(event, responder) => {
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
                        self.event_sender.send(Event::Exit);
                    }
                }
            }
        }
    }

    let (sender, mut receiver) = futures::channel::mpsc::unbounded::<Event<BouncerEvent>>();
    let event_sender = EventSender::<Event<BouncerEvent>>(sender);
    event_sender.send(Event::Init);

    while let Some(event) = receiver.next().await {
        if matches!(event, Event::Exit) {
            receiver.close();
        }
        event_handler(event).expect("Failed to handle event");
    }
```

## Test

The library provides a hermetic-integration test that aims for full code coverage.

```
$ fx test appkit-tests
```
