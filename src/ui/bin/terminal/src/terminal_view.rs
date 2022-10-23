// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::key_util::get_input_sequence_for_key_event,
    crate::ui::{
        PointerEventResponse, ScrollContext, TerminalConfig, TerminalFacet, TerminalMessages,
        TerminalScene,
    },
    anyhow::{Context as _, Error},
    carnelian::{
        color::Color,
        drawing::load_font,
        input::{self},
        render::Context as RenderContext,
        scene::{
            facets::FacetId,
            scene::{Scene, SceneBuilder, SceneOrder},
        },
        AppSender, Message, Size, ViewAssistant, ViewAssistantContext, ViewKey,
    },
    euclid::size2,
    fidl_fuchsia_hardware_pty::WindowSize,
    fuchsia_async as fasync, fuchsia_trace as ftrace,
    futures::{channel::mpsc, io::AsyncReadExt, select, FutureExt, StreamExt},
    pty::ServerPty,
    std::{
        any::Any, cell::RefCell, convert::TryFrom, ffi::CStr, ffi::CString, fs::File,
        io::prelude::*, path::PathBuf, rc::Rc,
    },
    term_model::{
        ansi::Processor,
        clipboard::Clipboard,
        event::{Event, EventListener},
        grid::Scroll,
        index::{Column, Line, Point},
        term::{SizeInfo, TermMode},
        Term,
    },
    terminal::{cell_size_from_cell_height, get_scale_factor, FontSet},
    tracing::error,
};

// Font files.
const FONT: &'static str = "/pkg/data/font.ttf";
const BOLD_FONT: &'static str = "/pkg/data/bold-font.ttf";
const ITALIC_FONT: &'static str = "/pkg/data/italic-font.ttf";
const BOLD_ITALIC_FONT: &'static str = "/pkg/data/bold-italic-font.ttf";
const FALLBACK_FONT_PREFIX: &'static str = "/pkg/data/fallback-font";

// Default font size.
const FONT_SIZE: f32 = 16.0;

// Amount of change to font size when zooming.
const FONT_SIZE_INCREMENT: f32 = 4.0;

// Font size limits.
const MIN_FONT_SIZE: f32 = 16.0;
const MAX_FONT_SIZE: f32 = 160.0;

// Maximum terminal size in cells. We support up to 4 layers per cell.
const MAX_CELLS: u32 = SceneOrder::MAX.as_u32() / 4;

// Terminal background color.
const BACKGROUND_COLOR: Color = Color { r: 0, g: 0, b: 0, a: 255 };

// DPI buckets used to determine the scale factor.
const DPI: &[u32] = &[160, 240, 320, 480];

#[cfg(test)]
use cstr::cstr;

const BYTE_BUFFER_MAX_SIZE: usize = 128;

struct ResizeEvent {
    window_size: WindowSize,
}

#[derive(Clone)]
struct AppSenderWrapper {
    app_sender: Option<AppSender>,
    test_sender: Option<mpsc::UnboundedSender<Message>>,
}

impl AppSenderWrapper {
    fn request_render(&self, target: ViewKey) {
        if let Some(app_sender) = &self.app_sender {
            app_sender.request_render(target);
        } else if let Some(sender) = &self.test_sender {
            sender
                .unbounded_send(Box::new("request_render"))
                .expect("Unable queue message to test_sender");
        }
    }

    #[cfg(test)]
    // Allows tests to observe what is sent to the app_sender.
    fn use_test_sender(&mut self, sender: mpsc::UnboundedSender<Message>) {
        self.app_sender = None;
        self.test_sender = Some(sender);
    }
}

struct PtyContext {
    resize_sender: mpsc::UnboundedSender<ResizeEvent>,
    file: File,
    resize_receiver: Option<mpsc::UnboundedReceiver<ResizeEvent>>,
    test_buffer: Option<Vec<u8>>,
}

impl PtyContext {
    fn from_pty(pty: &ServerPty) -> Result<PtyContext, Error> {
        let (resize_sender, resize_receiver) = mpsc::unbounded();
        let file = pty.try_clone_fd()?;
        Ok(PtyContext {
            resize_sender,
            file,
            resize_receiver: Some(resize_receiver),
            test_buffer: None,
        })
    }

    fn take_resize_receiver(&mut self) -> mpsc::UnboundedReceiver<ResizeEvent> {
        self.resize_receiver.take().expect("attempting to take resize receiver")
    }

    #[cfg(test)]
    fn allow_dual_write_for_test(&mut self) {
        self.test_buffer = Some(vec![]);
    }
}

impl Write for PtyContext {
    fn write(&mut self, buf: &[u8]) -> Result<usize, std::io::Error> {
        if let Some(test_buffer) = self.test_buffer.as_mut() {
            for b in buf {
                test_buffer.push(*b);
            }
        }
        self.file.write(buf)
    }

    fn flush(&mut self) -> Result<(), std::io::Error> {
        self.file.flush()
    }
}

struct EventProxy {
    app_sender: AppSenderWrapper,
    view_key: ViewKey,
}

impl EventListener for EventProxy {
    fn send_event(&self, event: Event) {
        match event {
            Event::MouseCursorDirty => {
                self.app_sender.request_render(self.view_key);
            }
            _ => (),
        }
    }
}

trait PointerEventResponseHandler {
    /// Signals that the struct should queue a view update.
    fn update_view(&mut self);

    fn scroll_term(&mut self, scroll: Scroll);
}

struct PointerEventResponseHandlerImpl<'a> {
    ctx: &'a mut ViewAssistantContext,
    term: Rc<RefCell<Term<EventProxy>>>,
}

impl PointerEventResponseHandler for PointerEventResponseHandlerImpl<'_> {
    fn update_view(&mut self) {
        self.ctx.request_render();
    }

    fn scroll_term(&mut self, scroll: Scroll) {
        let mut term = self.term.borrow_mut();
        term.scroll_display(scroll);
    }
}

struct SceneDetails {
    scene: Scene,
    terminal: FacetId,
}

pub struct TerminalViewAssistant {
    last_known_size: Size,
    last_known_size_info: SizeInfo,
    pty_context: Option<PtyContext>,
    terminal_scene: TerminalScene,
    term: Rc<RefCell<Term<EventProxy>>>,
    app_sender: AppSenderWrapper,
    view_key: ViewKey,
    scroll_to_bottom_on_input: bool,
    font_set: FontSet,
    font_size: f32,
    scene_details: Option<SceneDetails>,

    /// If non-empty, will use this command when spawning the pty.
    spawn_command: Vec<CString>,

    /// If non-empty, will use this environment when spawning the pty.
    spawn_environ: Vec<CString>,
}

impl TerminalViewAssistant {
    /// Creates a new instance of the TerminalViewAssistant.
    pub fn new(
        app_sender: &AppSender,
        view_key: ViewKey,
        scroll_to_bottom_on_input: bool,
        spawn_command: Vec<CString>,
        spawn_environ: Vec<CString>,
    ) -> TerminalViewAssistant {
        let font = load_font(PathBuf::from(FONT)).expect("unable to load font data");
        let bold_font = load_font(PathBuf::from(BOLD_FONT)).expect("unable to load bold font data");
        let italic_font =
            load_font(PathBuf::from(ITALIC_FONT)).expect("unable to load italic font data");
        let bold_italic_font = load_font(PathBuf::from(BOLD_ITALIC_FONT))
            .expect("unable to load bold italic font data");
        let mut fallback_fonts = vec![];
        while let Ok(font) = load_font(PathBuf::from(format!(
            "{}-{}.ttf",
            FALLBACK_FONT_PREFIX,
            fallback_fonts.len() + 1
        ))) {
            fallback_fonts.push(font);
        }
        let font_set = FontSet::new(
            font,
            Some(bold_font),
            Some(italic_font),
            Some(bold_italic_font),
            fallback_fonts,
        );
        let cell_size = cell_size_from_cell_height(&font_set, FONT_SIZE);
        let size_info = SizeInfo {
            // set the initial size/width to be that of the cell size which prevents
            // the term from panicking if a byte is received before a resize event.
            width: cell_size.width * 80.0,
            height: cell_size.height * 24.0,
            cell_width: cell_size.width,
            cell_height: cell_size.height,
            padding_x: 0.0,
            padding_y: 0.0,
            dpr: 1.0,
        };

        let terminal_scene = TerminalScene::new(app_sender.clone(), view_key);

        let app_sender =
            AppSenderWrapper { app_sender: Some(app_sender.clone()), test_sender: None };

        let event_proxy = EventProxy { app_sender: app_sender.clone(), view_key };

        let term = Term::new(&TerminalConfig::default(), &size_info, Clipboard::new(), event_proxy);

        TerminalViewAssistant {
            last_known_size: Size::zero(),
            last_known_size_info: size_info,
            pty_context: None,
            term: Rc::new(RefCell::new(term)),
            terminal_scene,
            app_sender,
            view_key,
            font_set,
            font_size: FONT_SIZE,
            scene_details: None,
            spawn_command,
            spawn_environ,
            scroll_to_bottom_on_input,
        }
    }

    #[cfg(test)]
    pub fn new_for_test() -> TerminalViewAssistant {
        let app_sender = AppSender::new_for_testing_purposes_only();
        Self::new(
            &app_sender,
            Default::default(),
            false,
            vec![cstr!("/pkg/bin/sh").to_owned()],
            vec![],
        )
    }

    /// Checks if we need to perform a resize based on a new size.
    /// This method rounds pixels down to the next pixel value.
    fn needs_resize(prev_size: &Size, new_size: &Size) -> bool {
        prev_size.floor().not_equal(new_size.floor()).any()
    }

    /// Checks to see if the size of terminal has changed and resizes if it has.
    fn resize_if_needed(&mut self, new_size: &Size, _metrics: &Size) -> Result<(), Error> {
        // The shell works on logical size units but the views operate based on the size
        if TerminalViewAssistant::needs_resize(&self.last_known_size, new_size) {
            let term_size = new_size.floor();

            // TODO(fxbug.dev/91053): Use physical size relative to largest display that
            // terminal is visible on determine DPI.
            //
            // const MM_PER_INCH: f32 = 25.4;
            // let dpi = context.size.height * MM_PER_INCH / vertical_size_mm as f32;
            //
            // We assume 160 DPI for now. This is a good assumption for a 1080p laptop.
            const TEMPORARY_DPI_HACK: f32 = 160.0;
            let dpi = TEMPORARY_DPI_HACK;

            // Get scale factor given our set of DPI buckets.
            let scale_factor = get_scale_factor(&DPI.iter().cloned().collect(), dpi);

            let cell_height = self.font_size * scale_factor;
            let cell_size = cell_size_from_cell_height(&self.font_set, cell_height);
            let grid_size =
                Size::new(term_size.width / cell_size.width, term_size.height / cell_size.height)
                    .floor();
            // Clamp width to respect `MAX_CELLS`.
            let clamped_grid_size = if grid_size.area() > MAX_CELLS as f32 {
                assert!(
                    grid_size.height <= MAX_CELLS as f32,
                    "terminal height greater than MAX_CELLS: {}",
                    grid_size.height
                );
                Size::new(MAX_CELLS as f32 / grid_size.height, grid_size.height).floor()
            } else {
                grid_size
            };
            let clamped_size = Size::new(
                clamped_grid_size.width * cell_size.width,
                clamped_grid_size.height * cell_size.height,
            );
            let term_size_info = SizeInfo {
                width: clamped_size.width,
                height: clamped_size.height,
                cell_width: cell_size.width,
                cell_height: cell_size.height,
                padding_x: 0.0,
                padding_y: 0.0,
                dpr: 1.0,
            };

            // we can safely call borrow_mut here because we are running the terminal
            // in single threaded mode. If we do move to a multithreaded model we will
            // get a compiler error since we are using spawn_local in our pty_loop.
            self.term.borrow_mut().resize(&term_size_info);

            // PTY window size (in character cells).
            let window_size = WindowSize {
                width: clamped_grid_size.width as u32,
                height: clamped_grid_size.height as u32,
            };

            self.queue_resize_event(ResizeEvent { window_size })
                .context("unable to queue outgoing pty message")?;

            self.last_known_size = *new_size;
            self.last_known_size_info = term_size_info;
            self.terminal_scene.update_size(term_size, cell_size);
            self.scene_details = None;
        }
        Ok(())
    }

    /// Checks to see if the Pty has been spawned and if not it does so.
    fn spawn_pty_loop(&mut self) -> Result<(), Error> {
        if self.pty_context.is_some() {
            return Ok(());
        }

        let pty = ServerPty::new()?;
        let mut pty_context = PtyContext::from_pty(&pty)?;
        let mut resize_receiver = pty_context.take_resize_receiver();

        let app_sender = self.app_sender.clone();
        let view_key = self.view_key;

        let term_clone = self.term.clone();
        let spawn_command = self.spawn_command.clone();
        let spawn_environ = self.spawn_environ.clone();

        // We want spawn_local here to enforce the single threaded model. If we
        // do move to multithreaded we will need to refactor the term parsing
        // logic to account for thread safaty.
        fasync::Task::local(async move {
            let environ: Vec<&CStr> = spawn_environ.iter().map(|s| s.as_ref()).collect();
            let process = if spawn_command.is_empty() {
                pty.spawn(None, Some(environ.as_slice())).await.expect("unable to spawn pty")
            } else {
                let argv: Vec<&CStr> = spawn_command.iter().map(|s| s.as_ref()).collect();
                pty.spawn_with_argv(&argv[0], argv.as_slice(), Some(environ.as_slice()))
                    .await
                    .expect("unable to spawn pty")
            };

            let fd = process.pty.try_clone_fd().expect("unable to clone pty read fd");
            let mut evented_fd = unsafe {
                // EventedFd::new() is unsafe because it can't guarantee the lifetime of
                // the file descriptor passed to it exceeds the lifetime of the EventedFd.
                // Since we're cloning the file when passing it in, the EventedFd
                // effectively owns that file descriptor and thus controls it's lifetime.
                fasync::net::EventedFd::new(fd).expect("failed to create evented_fd for io_loop")
            };

            let mut write_fd = process.pty.try_clone_fd().expect("unable to clone pty write fd");
            let mut parser = Processor::new();

            let mut read_buf = [0u8; BYTE_BUFFER_MAX_SIZE];
            loop {
                let mut read_fut = evented_fd.read(&mut read_buf).fuse();
                select!(
                    result = read_fut => {
                        let read_count = result.unwrap_or_else(|e: std::io::Error| {
                            error!(
                                "failed to read bytes from io_loop, dropping current message: {:?}",
                                e
                            );
                            0
                        });
                        ftrace::duration!("terminal", "parse_bytes", "len" => read_count as u32);
                        let mut term = term_clone.borrow_mut();
                        if read_count > 0 {
                            for byte in &read_buf[0..read_count] {
                                parser.advance(&mut *term, *byte, &mut write_fd);
                            }
                            app_sender.request_render(view_key);
                        }
                    },
                    result = resize_receiver.next().fuse() => {
                        if let Some(event) = result {
                            process.pty.resize(event.window_size).await.unwrap_or_else(|e: anyhow::Error| {
                                error!("failed to send resize message to pty: {:?}", e)
                            });
                            app_sender.request_render(view_key);
                        }
                    }
                );
                if !process.is_running() {
                    break;
                }
            }
            // TODO(fxb/60181): Exit by using Carnelian, when implemented.
            std::process::exit(
                match process
                    .process_info()
                    .and_then(|info| i32::try_from(info.return_code).context("overflow"))
                {
                    Ok(return_code) => return_code,
                    error => {
                        error!("failed to obtain the shell process return code: {:?}", error);
                        1
                    }
                },
            );
        })
        .detach();

        self.pty_context = Some(pty_context);

        Ok(())
    }

    fn queue_resize_event(&mut self, event: ResizeEvent) -> Result<(), Error> {
        if let Some(pty_context) = &mut self.pty_context {
            pty_context.resize_sender.unbounded_send(event).context("Unable send resize event")?;
        }

        Ok(())
    }

    fn set_font_size(&mut self, font_size: f32) {
        self.font_size = font_size;
        self.last_known_size = Size::zero();
        self.app_sender.request_render(self.view_key);
    }

    // This method is overloaded from the ViewAssistant trait so we can test the method.
    // The ViewAssistant trait requires a ViewAssistantContext which we do not use and
    // we cannot make. This allows us to call the method directly in the tests.
    fn handle_keyboard_event_internal(
        &mut self,
        event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        if self.handle_control_keyboard_event(event)? {
            return Ok(());
        }

        let app_cursor = self.term.borrow().mode().contains(TermMode::APP_CURSOR);
        if let Some(string) = get_input_sequence_for_key_event(event, app_cursor) {
            // In practice these writes will contain a small amount of data
            // so we can use a synchronous write. If that proves to not be the
            // case we will need to refactor to have buffered writing.
            if let Some(pty_context) = &mut self.pty_context {
                pty_context
                    .write_all(string.as_bytes())
                    .unwrap_or_else(|e| println!("failed to write character to pty: {}", e));
            }

            // Scroll to bottom on input if enabled.
            if self.scroll_to_bottom_on_input {
                let mut term = self.term.borrow_mut();
                term.scroll_display(Scroll::Bottom);
            }
        }

        Ok(())
    }

    /// Handles the pointer event response.
    fn handle_pointer_event_response<'a, T: PointerEventResponseHandler>(
        &mut self,
        response: PointerEventResponse,
        handler: &'a mut T,
    ) {
        match response {
            PointerEventResponse::ScrollLines(lines) => {
                handler.scroll_term(Scroll::Lines(lines));
                // Show scroll thumb after scrolling.
                self.terminal_scene.show_scroll_thumb();
            }
            PointerEventResponse::ViewDirty => handler.update_view(),
        }
    }

    fn handle_control_keyboard_event(
        &mut self,
        event: &input::keyboard::Event,
    ) -> Result<bool, Error> {
        match event.phase {
            input::keyboard::Phase::Pressed | input::keyboard::Phase::Repeat => {
                let modifiers = &event.modifiers;
                match event.code_point {
                    None => {
                        const HID_USAGE_KEY_HOME: u32 = 0x4a;
                        const HID_USAGE_KEY_PAGEUP: u32 = 0x4b;
                        const HID_USAGE_KEY_END: u32 = 0x4d;
                        const HID_USAGE_KEY_PAGEDOWN: u32 = 0x4e;
                        const HID_USAGE_KEY_DOWN: u32 = 0x51;
                        const HID_USAGE_KEY_UP: u32 = 0x52;

                        let maybe_scroll = match event.hid_usage {
                            HID_USAGE_KEY_UP if modifiers.alt => Some(Scroll::Lines(1)),
                            HID_USAGE_KEY_DOWN if modifiers.alt => Some(Scroll::Lines(-1)),
                            HID_USAGE_KEY_PAGEUP if modifiers.shift => Some(Scroll::PageUp),
                            HID_USAGE_KEY_PAGEDOWN if modifiers.shift => Some(Scroll::PageDown),
                            HID_USAGE_KEY_HOME if modifiers.shift => Some(Scroll::Top),
                            HID_USAGE_KEY_END if modifiers.shift => Some(Scroll::Bottom),
                            _ => None,
                        };

                        if let Some(scroll) = maybe_scroll {
                            self.term.borrow_mut().scroll_display(scroll);
                            // Show scroll thumb after scrolling.
                            self.terminal_scene.show_scroll_thumb();
                            return Ok(true);
                        }
                    }
                    Some(code_point) if modifiers.alt == true => {
                        const PLUS: u32 = 43;
                        const EQUAL: u32 = 61;
                        const MINUS: u32 = 45;

                        match code_point {
                            PLUS | EQUAL => {
                                let new_font_size =
                                    (self.font_size + FONT_SIZE_INCREMENT).min(MAX_FONT_SIZE);
                                self.set_font_size(new_font_size);
                                return Ok(true);
                            }
                            MINUS => {
                                let new_font_size =
                                    (self.font_size - FONT_SIZE_INCREMENT).max(MIN_FONT_SIZE);
                                self.set_font_size(new_font_size);
                                return Ok(true);
                            }
                            _ => {}
                        }
                    }
                    _ => {}
                }
            }
            _ => {}
        }

        Ok(false)
    }
}

impl ViewAssistant for TerminalViewAssistant {
    fn setup(&mut self, _context: &ViewAssistantContext) -> Result<(), Error> {
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: fuchsia_zircon::Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        ftrace::duration!("terminal", "TerminalViewAssistant:render");

        // we need to call spawn in this update block because calling it in the
        // setup method causes us to receive write events before the view is
        // prepared to draw.
        self.spawn_pty_loop()?;
        self.resize_if_needed(&context.size, &context.metrics)?;

        let mut scene_details = self.scene_details.take().unwrap_or_else(|| {
            let mut builder = SceneBuilder::new().background_color(BACKGROUND_COLOR).mutable(false);

            let cell_size =
                size2(self.last_known_size_info.cell_width, self.last_known_size_info.cell_height);
            let terminal = builder.facet(Box::new(TerminalFacet::new(
                self.app_sender.app_sender.clone(),
                self.view_key,
                self.font_set.clone(),
                &cell_size,
                self.term.clone(),
            )));

            SceneDetails { scene: builder.build(), terminal }
        });

        let term = self.term.borrow();
        let grid = term.grid();

        let scroll_context = ScrollContext {
            history: grid.history_size(),
            visible_lines: *grid.num_lines(),
            display_offset: grid.display_offset(),
        };
        self.terminal_scene.update_scroll_context(scroll_context);

        // Write the grid to inspect for e2e testing. The contents will trim all whitespace
        // from either end of the string which means that the trailing space after the prompt
        // will not be included.
        let bottom = grid.display_offset();
        let top = bottom + *grid.num_lines() - 1;

        let txt = term.bounds_to_string(
            Point::new(*Line(top), Column(0)),
            Point::new(*Line(bottom), grid.num_cols()),
        );
        fuchsia_inspect::component::inspector().root().record_string("grid", txt.trim());

        #[allow(clippy::drop_ref)] // TODO(fxbug.dev/95065)
        drop(grid);

        scene_details.scene.render(render_context, ready_event, context)?;
        self.scene_details = Some(scene_details);

        Ok(())
    }

    fn handle_keyboard_event(
        &mut self,
        _context: &mut ViewAssistantContext,
        _event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        self.handle_keyboard_event_internal(keyboard_event)?;
        Ok(())
    }

    fn handle_mouse_event(
        &mut self,
        ctx: &mut ViewAssistantContext,
        _event: &input::Event,
        mouse_event: &input::mouse::Event,
    ) -> Result<(), Error> {
        if let Some(response) = self.terminal_scene.handle_mouse_event(mouse_event, ctx) {
            let mut handler = PointerEventResponseHandlerImpl { ctx, term: self.term.clone() };
            self.handle_pointer_event_response(response, &mut handler);
        }
        Ok(())
    }

    fn handle_touch_event(
        &mut self,
        ctx: &mut ViewAssistantContext,
        _event: &input::Event,
        touch_event: &input::touch::Event,
    ) -> Result<(), Error> {
        if let Some(response) = self.terminal_scene.handle_touch_event(touch_event, ctx) {
            let mut handler = PointerEventResponseHandlerImpl { ctx, term: self.term.clone() };
            self.handle_pointer_event_response(response, &mut handler);
        }
        Ok(())
    }

    fn handle_message(&mut self, message: Box<dyn Any>) {
        if let Some(message) = message.downcast_ref::<TerminalMessages>() {
            match message {
                TerminalMessages::SetScrollThumbMessage(thumb) => {
                    if !thumb.is_some() {
                        self.terminal_scene.hide_scroll_thumb();
                    }

                    // Forward message to the terminal facet.
                    if let Some(scene_details) = &mut self.scene_details {
                        scene_details.scene.send_message(
                            &scene_details.terminal,
                            Box::new(TerminalMessages::SetScrollThumbMessage(*thumb)),
                        );
                    }
                }
                TerminalMessages::SetScrollThumbFadeOutMessage(thumb_fade_out) => {
                    // Forward message to the terminal facet.
                    if let Some(scene_details) = &mut self.scene_details {
                        scene_details.scene.send_message(
                            &scene_details.terminal,
                            Box::new(TerminalMessages::SetScrollThumbFadeOutMessage(
                                *thumb_fade_out,
                            )),
                        );
                    }
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::anyhow,
        fuchsia_async::{DurationExt, Timer},
        fuchsia_zircon::DurationNum,
        futures::future::Either,
        term_model::grid::Scroll,
    };

    fn unit_metrics() -> Size {
        Size::new(1.0, 1.0)
    }

    #[test]
    fn can_create_view() {
        let _ = TerminalViewAssistant::new_for_test();
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_pointer_event_response_updates_view_for_view_dirty() -> Result<(), Error> {
        let mut view = TerminalViewAssistant::new_for_test();
        let mut handler = TestPointerEventResponder::new();
        view.handle_pointer_event_response(PointerEventResponse::ViewDirty, &mut handler);
        assert_eq!(handler.update_count, 1);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_pointer_event_response_does_not_update_view_for_scroll_lines(
    ) -> Result<(), Error> {
        let mut view = TerminalViewAssistant::new_for_test();

        let mut handler = TestPointerEventResponder::new();
        view.handle_pointer_event_response(PointerEventResponse::ScrollLines(1), &mut handler);
        assert_eq!(handler.update_count, 0);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_pointer_event_response_scroll_lines_updates_grid() -> Result<(), Error> {
        let mut view = TerminalViewAssistant::new_for_test();

        let mut handler = TestPointerEventResponder::new();
        view.handle_pointer_event_response(PointerEventResponse::ScrollLines(1), &mut handler);

        assert_eq!(handler.scroll_offset, 1);
        Ok(())
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
        view.resize_if_needed(&new_size, &unit_metrics()).expect("call to resize failed");
        let size_info = view.last_known_size_info.clone();

        // we want to make sure that the values are floored and that they
        // match what the scene will render the terminal as.

        // TODO(fxbug.dev/106720): Remove calculations' dependency on precise font metrics
        // assert_eq!(size_info.width, 100.0);
        // assert_eq!(size_info.height, 100.0);

        assert!((size_info.width - 100.0).abs() <= 1.0, "size_info {size_info:?}");
        assert!((size_info.height - 100.0).abs() <= 1.0, "size_info {size_info:?}");
    }

    #[fasync::run_singlethreaded(test)]
    async fn event_proxy_calls_view_update_on_dirty_mouse_cursor() -> Result<(), Error> {
        let (sender, mut receiver) = mpsc::unbounded();
        let app_sender = AppSenderWrapper { app_sender: None, test_sender: Some(sender) };

        let event_proxy = EventProxy { app_sender, view_key: Default::default() };

        event_proxy.send_event(Event::MouseCursorDirty);

        wait_until_update_received_or_timeout(&mut receiver)
            .await
            .context(":event_proxy_calls_view_update_on_dirty_mouse_cursor failed to get update")?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn scroll_display_triggers_call_to_redraw() -> Result<(), Error> {
        let (view, mut receiver) = make_test_view_with_spawned_pty_loop().await?;

        let event_proxy =
            EventProxy { app_sender: view.app_sender.clone(), view_key: view.view_key };

        let mut term = Term::new(
            &TerminalConfig::default(),
            &view.last_known_size_info,
            Clipboard::new(),
            event_proxy,
        );

        term.scroll_display(Scroll::Lines(1));

        // No redraw will trigger a timeout and failure
        wait_until_update_received_or_timeout(&mut receiver)
            .await
            .context(":resize_message_triggers_call_to_redraw after queue event")?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn resize_message_queued_with_logical_size_when_resize_needed() -> Result<(), Error> {
        let pty = ServerPty::new()?;
        let mut pty_context = PtyContext::from_pty(&pty)?;
        let mut view = TerminalViewAssistant::new_for_test();
        let mut receiver = pty_context.take_resize_receiver();

        view.pty_context = Some(pty_context);

        view.resize_if_needed(&Size::new(800.0, 1600.0), &unit_metrics())
            .expect("call to resize failed");

        let event = receiver.next().await.expect("failed to receive pty event");

        // TODO(fxbug.dev/106720): Remove calculations' dependency on precise font metrics
        // assert_eq!(event.window_size.width, 80);
        // assert_eq!(event.window_size.height, 80);

        assert!((event.window_size.width as i64 - 80).abs() < 10);
        assert!((event.window_size.height as i64 - 80).abs() < 10);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_keyboard_event_writes_characters() -> Result<(), Error> {
        let pty = ServerPty::new()?;
        let mut pty_context = PtyContext::from_pty(&pty)?;
        let mut view = TerminalViewAssistant::new_for_test();
        pty_context.allow_dual_write_for_test();

        view.pty_context = Some(pty_context);

        let capital_a = 65;
        let alt_modifier = false;
        view.handle_keyboard_event_internal(&make_keyboard_event(capital_a, alt_modifier))?;

        let test_buffer = view.pty_context.as_mut().unwrap().test_buffer.take().unwrap();
        assert_eq!(test_buffer, b"A");

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_control_keyboard_event() -> Result<(), Error> {
        let pty = ServerPty::new()?;
        let mut pty_context = PtyContext::from_pty(&pty)?;
        let mut view = TerminalViewAssistant::new_for_test();
        pty_context.allow_dual_write_for_test();

        let (sender, mut _receiver) = mpsc::unbounded();
        view.app_sender = AppSenderWrapper { app_sender: None, test_sender: Some(sender) };
        view.pty_context = Some(pty_context);

        let equal = 61;
        let alt_modifier = true;
        view.handle_keyboard_event_internal(&make_keyboard_event(equal, alt_modifier))?;

        assert_eq!(view.font_size, 20.0);

        let test_buffer = view.pty_context.as_mut().unwrap().test_buffer.take().unwrap();
        assert_eq!(test_buffer, b"");

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn pty_is_spawned_on_first_request() -> Result<(), Error> {
        let mut view = TerminalViewAssistant::new_for_test();
        view.spawn_pty_loop()?;
        assert!(view.pty_context.is_some());

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn pty_message_reads_triggers_call_to_redraw() -> Result<(), Error> {
        let (view, mut receiver) = make_test_view_with_spawned_pty_loop().await?;

        let mut fd = view
            .pty_context
            .as_ref()
            .map(|ctx| ctx.file.try_clone().expect("attempt to clone fd failed"))
            .unwrap();

        fasync::Task::local(async move {
            let () = fd.write_all(b"ls").unwrap();
        })
        .detach();

        // No redraw will trigger a timeout and failure
        wait_until_update_received_or_timeout(&mut receiver)
            .await
            .context(":pty_message_reads_triggers_call_to_redraw after write")?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn resize_message_triggers_call_to_redraw() -> Result<(), Error> {
        let (mut view, mut receiver) = make_test_view_with_spawned_pty_loop().await?;

        let window_size = WindowSize { width: 123, height: 123 };

        view.queue_resize_event(ResizeEvent { window_size })
            .context("unable to queue outgoing pty message")?;

        // No redraw will trigger a timeout and failure
        wait_until_update_received_or_timeout(&mut receiver)
            .await
            .context(":resize_message_triggers_call_to_redraw after queue event")?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    #[ignore] // TODO(fxbug.dev/52560) re-enable this test when de-flaked
    async fn bytes_written_are_processed_by_term() -> Result<(), Error> {
        let (mut view, mut receiver) = make_test_view_with_spawned_pty_loop().await?;

        // make sure we have a big enough size that a single character does not wrap
        let large_size = Size::new(1000.0, 1000.0);
        view.resize_if_needed(&large_size, &unit_metrics())?;

        // Resizing will cause an update so we need to wait for that before we write.
        wait_until_update_received_or_timeout(&mut receiver)
            .await
            .context(":bytes_written_are_processed_by_term after resize_if_needed")?;

        // TODO: this variable triggered the `must_not_suspend` lint and may be held across an await
        // If this is the case, it is an error. See fxbug.dev/87757 for more details
        let term = view.term.borrow();

        let col_pos_before = term.cursor().point.col;
        drop(term);

        let mut fd = view
            .pty_context
            .as_ref()
            .map(|ctx| ctx.file.try_clone().expect("attempt to clone fd failed"))
            .unwrap();

        fasync::Task::local(async move {
            let () = fd.write_all(b"A").unwrap();
        })
        .detach();

        // Wait until we get a notice that the view is ready to redraw
        wait_until_update_received_or_timeout(&mut receiver)
            .await
            .context(":bytes_written_are_processed_by_term after write")?;

        let term = view.term.borrow();
        let col_pos_after = term.cursor().point.col;
        assert_eq!(col_pos_before + 1, col_pos_after);

        Ok(())
    }

    async fn make_test_view_with_spawned_pty_loop(
    ) -> Result<(TerminalViewAssistant, mpsc::UnboundedReceiver<Message>), Error> {
        let (sender, mut receiver) = mpsc::unbounded();

        let mut view = TerminalViewAssistant::new_for_test();
        view.app_sender.use_test_sender(sender);

        let () = view.spawn_pty_loop().unwrap();

        // Spawning the loop triggers a read and a redraw, we want to skip this
        // so that we can check that our test event triggers the redraw.
        wait_until_update_received_or_timeout(&mut receiver)
            .await
            .context("::make_test_view_with_spawned_pty_loop")?;

        Ok((view, receiver))
    }

    async fn wait_until_update_received_or_timeout(
        receiver: &mut mpsc::UnboundedReceiver<Message>,
    ) -> Result<(), Error> {
        #[allow(clippy::never_loop)] // TODO(fxbug.dev/95065)
        loop {
            let timeout = Timer::new(5000_i64.millis().after_now());
            let either = futures::future::select(timeout, receiver.next().fuse());
            let resolved = either.await;
            match resolved {
                Either::Left(_) => {
                    return Err(anyhow!("wait_until_update_received timed out"));
                }
                Either::Right((result, _)) => {
                    let _: Message = result.expect("result should not be None");
                    break;
                }
            }
        }
        Ok(())
    }

    fn make_keyboard_event(code_point: u32, alt_modifier: bool) -> input::keyboard::Event {
        let modifiers = if alt_modifier {
            input::Modifiers { alt: true, ..input::Modifiers::default() }
        } else {
            input::Modifiers::default()
        };
        input::keyboard::Event {
            code_point: Some(code_point),
            phase: input::keyboard::Phase::Pressed,
            hid_usage: 0 as u32,
            modifiers,
        }
    }

    struct TestPointerEventResponder {
        update_count: usize,
        scroll_offset: isize,
    }

    impl TestPointerEventResponder {
        fn new() -> TestPointerEventResponder {
            TestPointerEventResponder { update_count: 0, scroll_offset: 0 }
        }
    }

    impl PointerEventResponseHandler for TestPointerEventResponder {
        fn update_view(&mut self) {
            self.update_count += 1;
        }

        fn scroll_term(&mut self, scroll: Scroll) {
            match scroll {
                Scroll::Lines(lines) => self.scroll_offset += lines,
                _ => (),
            }
        }
    }
}
