// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::key_util::get_input_sequence_for_key_event,
    crate::pty::Pty,
    crate::ui::TerminalScene,
    anyhow::{Context as _, Error},
    carnelian::{
        make_message, AnimationMode, AppContext, Message, Size, ViewAssistant,
        ViewAssistantContext, ViewKey,
    },
    fidl_fuchsia_hardware_pty::WindowSize,
    fidl_fuchsia_ui_input::KeyboardEvent,
    fuchsia_async as fasync,
    futures::{
        channel::mpsc,
        io::{AsyncReadExt, AsyncWriteExt},
        select, FutureExt, StreamExt,
    },
    std::{cell::RefCell, fs::File, rc::Rc},
    term_model::{
        ansi::Processor,
        config::Config,
        term::{SizeInfo, Term},
    },
};

/// An enum representing messages that are incoming from the Pty.
enum PtyIncomingMessages {
    /// Message sent when a byte comes in from the Pty.
    ByteReceived(u8),
}

/// Messages which can be used to interact with the Pty.
enum PtyOutgoingMessages {
    /// A message which indicates that a String should be sent to the Pty.
    SendInput(String),

    /// Indicates that the logical size of the Pty should be updated.
    Resize(Size),
}

pub struct TerminalViewAssistant {
    last_known_size: Size,
    parser: Processor,
    pty: Option<PtyWrapper>,
    terminal_scene: TerminalScene,
    term: Term,
    app_context: AppContext,
}

impl TerminalViewAssistant {
    /// Creates a new instance of the TerminalViewAssistant.
    pub fn new(app_context: &AppContext) -> TerminalViewAssistant {
        let parser = Processor::new();
        let cell_size = Size::new(12.0, 22.0);
        let term = Term::new(
            &Config::default(),
            SizeInfo {
                // set the initial size/width to be that of the cell size which prevents
                // the term from panicing if a byte is received before a resize event.
                width: cell_size.width,
                height: cell_size.height,
                cell_width: cell_size.width,
                cell_height: cell_size.height,
                padding_x: 0.0,
                padding_y: 0.0,
            },
        );

        TerminalViewAssistant {
            last_known_size: Size::zero(),
            parser,
            pty: None,
            term,
            terminal_scene: TerminalScene::default(),
            app_context: app_context.clone(),
        }
    }

    #[cfg(test)]
    pub fn new_for_test() -> TerminalViewAssistant {
        let app_context = AppContext::new_for_testing_purposes_only();
        Self::new(&app_context)
    }

    /// Checks if we need to perform a resize based on a new size.
    /// This method rounds pixels down to the next pixel value.
    fn needs_resize(prev_size: &Size, new_size: &Size) -> bool {
        prev_size.floor().not_equal(&new_size.floor()).any()
    }

    /// Checks to see if the size of terminal has changed and resizes if it has.
    fn resize_if_needed(&mut self, new_size: &Size, logical_size: &Size) -> Result<(), Error> {
        // The shell works on logical size units but the views operate based on the size
        if TerminalViewAssistant::needs_resize(&self.last_known_size, new_size) {
            let floored_size = new_size.floor();
            let term_size = TerminalScene::calculate_term_size_from_size(&floored_size);

            let last_size_info = self.term.size_info();
            let cell_width = last_size_info.cell_width;
            let cell_height = last_size_info.cell_height;
            let padding_x = last_size_info.padding_x;
            let padding_y = last_size_info.padding_y;

            self.term.resize(&SizeInfo {
                width: term_size.width,
                height: term_size.height,
                cell_width,
                cell_height,
                padding_x,
                padding_y,
            });

            self.queue_outgoing_message(PtyOutgoingMessages::Resize(*logical_size))
                .context("unable to queue outgoing pty message")?;

            self.last_known_size = floored_size;
            self.terminal_scene.update_size(floored_size);
            self.terminal_scene.update_cell_size(Size::new(cell_width, cell_height));
        }
        Ok(())
    }

    /// Checks to see if the Pty has been spawned and if not it does so.
    fn spawn_pty_if_needed(&mut self, logical_size: &Size, view_key: ViewKey) -> Result<(), Error> {
        if self.pty.is_some() {
            return Ok(());
        }

        // need to have non zero size before we can spawn
        if logical_size.width < 1.0 || logical_size.height < 1.0 {
            return Ok(());
        }

        let mut pty_wrapper = PtyWrapper::new()?;
        let pty_clone = pty_wrapper.pty.clone();

        let window_size =
            WindowSize { width: logical_size.width as u32, height: logical_size.height as u32 };

        let mut pty_receiver = pty_wrapper.take_receiver();
        let app_context = self.app_context.clone();

        fasync::spawn_local(async move {
            let mut pty = pty_clone.borrow_mut();
            pty.spawn(window_size).await.expect("failed to spawn pty");

            let fd = pty.try_clone_fd().expect("unable to clone pty read fd");
            let mut evented_fd = unsafe {
                // EventedFd::new() is unsafe because it can't guarantee the lifetime of
                // the file descriptor passed to it exceeds the lifetime of the EventedFd.
                // Since we're cloning the file when passing it in, the EventedFd
                // effectively owns that file descriptor and thus controls it's lifetime.
                fasync::net::EventedFd::new(fd).expect("failed to create evented_fd for io_loop")
            };

            drop(pty);

            let mut read_buf = [0u8; 1024];
            loop {
                let mut read_fut = evented_fd.read(&mut read_buf).fuse();
                select!(
                    result = read_fut => {
                        let bytes_read = result.unwrap_or_else(|e: std::io::Error| {
                            eprintln!(
                                "failed to read bytes from io_loop, dropping current message: {:?}",
                                e
                            );
                            0
                        });

                        if bytes_read > 0 {
                            for byte in &read_buf[0..bytes_read] {
                                app_context.queue_message(view_key, make_message(PtyIncomingMessages::ByteReceived(*byte)));
                            }
                        }
                    },
                result = pty_receiver.next().fuse() => {
                        let message = result.expect("failed to unwrap pty send event");
                        match message {
                            PtyOutgoingMessages::SendInput(string) => {
                                evented_fd.write_all(string.as_bytes()).await.unwrap_or_else(|e| {
                                    println!("failed to write character to pty: {}", e)
                                });
                            },
                            PtyOutgoingMessages::Resize(size) => {
                                let pty = pty_clone.borrow_mut();
                                let window_size = WindowSize { width: size.width as u32, height: size.height as u32 };
                                pty.resize(window_size).await.unwrap_or_else(|e: anyhow::Error| {
                                    eprintln!("failed to send resize message to pty: {:?}", e)
                                });
                            }
                        }
                    }
                )
            }
        });

        self.pty = Some(pty_wrapper);

        Ok(())
    }

    fn queue_outgoing_message(&mut self, message: PtyOutgoingMessages) -> Result<(), Error> {
        if let Some(pty) = &mut self.pty {
            pty.sender.try_send(message).context("Unable queue pty message")?;
        }

        Ok(())
    }

    // This method is overloaded from the ViewAssistant trait so we can test the method.
    // The ViewAssistant trait requires a ViewAssistantContext which we do not use and
    // we cannot make. This allows us to call the method directly in the tests.
    fn handle_keyboard_event(&mut self, event: &KeyboardEvent) -> Result<(), Error> {
        if let Some(string) = get_input_sequence_for_key_event(event) {
            self.queue_outgoing_message(PtyOutgoingMessages::SendInput(string))?;
        }

        Ok(())
    }
}

impl ViewAssistant for TerminalViewAssistant {
    fn setup(&mut self, _context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        self.spawn_pty_if_needed(&context.logical_size, context.key)?;
        self.resize_if_needed(&context.size, &context.logical_size)?;

        // Tell the termnial scene to render the values
        let canvas = &mut context.canvas.as_ref().unwrap().borrow_mut();
        let config = Config::default();
        let iter =
            self.term.renderable_cells(&config, None /* selection */, true /* focused */);
        self.terminal_scene.render(canvas, iter);
        Ok(())
    }

    fn handle_keyboard_event(
        &mut self,
        _: &mut ViewAssistantContext<'_>,
        event: &KeyboardEvent,
    ) -> Result<(), Error> {
        self.handle_keyboard_event(event)
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        AnimationMode::EveryFrame
    }

    fn handle_message(&mut self, message: Message) {
        if let Some(pty_message) = message.downcast_ref::<PtyIncomingMessages>() {
            match pty_message {
                PtyIncomingMessages::ByteReceived(byte) => {
                    if let Some(pty) = &mut self.pty {
                        self.parser.advance(&mut self.term, *byte, &mut pty.fd);
                    } else {
                        self.parser.advance(&mut self.term, *byte, &mut ::std::io::sink());
                    }
                }
            }
        }
    }
}

struct PtyWrapper {
    fd: File,
    pty: Rc<RefCell<Pty>>,
    sender: mpsc::Sender<PtyOutgoingMessages>,
    receiver: Option<mpsc::Receiver<PtyOutgoingMessages>>,
}

impl PtyWrapper {
    fn new() -> Result<PtyWrapper, Error> {
        let pty = Pty::new()?;
        let (sender, receiver) = mpsc::channel(1024);

        Ok(PtyWrapper {
            fd: pty.try_clone_fd()?,
            pty: Rc::new(RefCell::new(pty)),
            receiver: Some(receiver),
            sender,
        })
    }

    fn take_receiver(&mut self) -> mpsc::Receiver<PtyOutgoingMessages> {
        self.receiver.take().expect("attempting to call PtyWrapper::take_receiver twice")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_ui_input::KeyboardEventPhase;

    #[test]
    fn can_create_view() {
        let _ = TerminalViewAssistant::new_for_test();
    }

    #[test]
    fn needs_resize_false_for_zero_sizes() {
        let zero = Size::zero();
        assert_eq!(TerminalViewAssistant::needs_resize(&zero, &zero), false);
    }

    #[test]
    fn needs_resize_true_for_different_sizes() {
        let prev_size = Size::zero();
        let new_size = Size::new(100.0, 100.0);
        assert!(TerminalViewAssistant::needs_resize(&prev_size, &new_size));
    }

    #[test]
    fn needs_resize_true_different_width_same_height() {
        let prev_size = Size::new(100.0, 10.0);
        let new_size = Size::new(100.0, 100.0);
        assert!(TerminalViewAssistant::needs_resize(&prev_size, &new_size));
    }

    #[test]
    fn needs_resize_true_different_height_same_width() {
        let prev_size = Size::new(10.0, 100.0);
        let new_size = Size::new(100.0, 100.0);
        assert!(TerminalViewAssistant::needs_resize(&prev_size, &new_size));
    }

    #[test]
    fn needs_resize_false_when_rounding_down() {
        let prev_size = Size::new(100.0, 100.0);
        let new_size = Size::new(100.1, 100.0);
        assert_eq!(TerminalViewAssistant::needs_resize(&prev_size, &new_size), false);
    }

    #[test]
    fn term_is_resized_when_needed() {
        let mut view = TerminalViewAssistant::new_for_test();
        let new_size = Size::new(100.5, 100.9);
        view.resize_if_needed(&new_size, &Size::zero()).expect("call to resize failed");

        let size_info = view.term.size_info();
        let expected_size = TerminalScene::calculate_term_size_from_size(&view.last_known_size);

        // we want to make sure that the values are floored and that they
        // match what the scene will render the terminal as.
        assert_eq!(size_info.width, expected_size.width);
        assert_eq!(size_info.height, expected_size.height);
    }

    #[test]
    fn last_known_size_is_floored_on_resize() {
        let mut view = TerminalViewAssistant::new_for_test();
        let new_size = Size::new(100.3, 100.4);
        view.resize_if_needed(&new_size, &Size::zero()).expect("call to resize failed");

        assert_eq!(view.last_known_size.width, 100.0);
        assert_eq!(view.last_known_size.height, 100.0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn resize_message_queued_with_logical_size_when_resize_needed() -> Result<(), Error> {
        let mut wrapper = PtyWrapper::new()?;
        let mut view = TerminalViewAssistant::new_for_test();
        let mut receiver = wrapper.take_receiver();

        view.pty = Some(wrapper);

        view.resize_if_needed(&Size::new(100.0, 100.0), &Size::new(1000.0, 2000.0))
            .expect("call to resize failed");

        let message = receiver.next().await.expect("failed to receive pty event");
        match message {
            PtyOutgoingMessages::Resize(size) => {
                assert_eq!(size.width, 1000.0);
                assert_eq!(size.height, 2000.0);
            }
            _ => assert!(false),
        }

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_keyboard_event_queues_characters() -> Result<(), Error> {
        let mut wrapper = PtyWrapper::new()?;
        let mut view = TerminalViewAssistant::new_for_test();
        let mut receiver = wrapper.take_receiver();

        view.pty = Some(wrapper);
        let capital_a = 65;
        view.handle_keyboard_event(&make_keyboard_event(capital_a))?;

        let message = receiver.next().await.expect("failed to receive pty event");
        match message {
            PtyOutgoingMessages::SendInput(c) => {
                assert_eq!(c, "A");
            }
            _ => assert!(false),
        }

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn pty_is_spawned_on_first_request() -> Result<(), Error> {
        let mut view = TerminalViewAssistant::new_for_test();
        view.spawn_pty_if_needed(&Size::new(100.0, 100.0), 1)?;
        assert!(view.pty.is_some());

        Ok(())
    }

    #[test]
    fn bytes_received_is_processed_by_term() {
        let mut view = TerminalViewAssistant::new_for_test();

        // make sure we have a big enough size that a single character does not wrap
        let large_size = Size::new(1000.0, 1000.0);
        let _ = view.resize_if_needed(&large_size, &large_size);

        let col_pos_before = view.term.cursor().point.col;

        view.handle_message(make_message(PtyIncomingMessages::ByteReceived(b'A')));

        let col_pos_after = view.term.cursor().point.col;
        assert_eq!(col_pos_before + 1, col_pos_after);
    }

    fn make_keyboard_event(code_point: u32) -> KeyboardEvent {
        KeyboardEvent {
            code_point: code_point,
            phase: KeyboardEventPhase::Pressed,
            device_id: 0 as u32,
            event_time: 0 as u64,
            hid_usage: 0 as u32,
            modifiers: 0 as u32,
        }
    }
}
