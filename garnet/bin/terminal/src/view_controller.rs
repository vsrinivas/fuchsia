use crate::pty::Pty;

use carnelian::{
    Canvas, Color, FontDescription, FontFace, IntSize, MappingPixelSink, Paint, Point, Size,
};

use failure::{Error, ResultExt};
use fidl::endpoints::{create_endpoints, ServerEnd};
use fidl_fuchsia_hardware_pty::WindowSize;
use fidl_fuchsia_images as images;
use fidl_fuchsia_ui_gfx as gfx;
use fidl_fuchsia_ui_input::{
    ImeServiceMarker, InputEvent, InputMethodAction, InputMethodEditorClientMarker,
    InputMethodEditorClientRequest, InputMethodEditorMarker, KeyboardEventPhase, KeyboardType,
    TextAffinity, TextInputState, TextRange, TextSelection,
};
use fidl_fuchsia_ui_scenic::{self as scenic, SessionListenerMarker, SessionListenerRequest};
use fidl_fuchsia_ui_views::ViewToken;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_scenic::{EntityNode, HostImageCycler, SessionPtr, View};

use futures::io::{AsyncReadExt, AsyncWriteExt};
use futures::{FutureExt, TryFutureExt, TryStreamExt};
use parking_lot::Mutex;
use std::{cell::RefCell, fs::File, rc::Rc, sync::Arc};
use term_model::ansi::Processor;
use term_model::config::Config;
use term_model::term::{SizeInfo, Term};

pub type FontFacePtr = Arc<Mutex<FontFace<'static>>>;

pub struct ViewController {
    face: FontFacePtr,
    session: SessionPtr,
    view: View,
    root_node: EntityNode,
    image_cycler: HostImageCycler,
    metrics: Option<gfx::Metrics>,
    logical_size: Option<gfx::Vec3>,
    term: Option<Term>,
    parser: Processor,
    output_buffer: Vec<u8>,
    pty: Rc<RefCell<Pty>>,
    pty_write_fd: File,
}

pub type ViewControllerPtr = Arc<Mutex<ViewController>>;

impl ViewController {
    pub fn new(
        face: FontFacePtr,
        view_token: ViewToken,
        session: SessionPtr,
        session_listener_request: ServerEnd<SessionListenerMarker>,
    ) -> Result<ViewControllerPtr, Error> {
        let ime_service = connect_to_service::<ImeServiceMarker>()?;
        let (ime_listener, ime_listener_request) = create_endpoints::<InputMethodEditorMarker>()?;
        let (ime_client_listener, ime_client_listener_request) =
            create_endpoints::<InputMethodEditorClientMarker>()?;
        let mut text_input_state = TextInputState {
            revision: 0,
            text: "".to_string(),
            selection: TextSelection { base: 0, extent: 0, affinity: TextAffinity::Upstream },
            composing: TextRange { start: 0, end: 0 },
        };
        ime_service.get_input_method_editor(
            KeyboardType::Text,
            InputMethodAction::None,
            &mut text_input_state,
            ime_client_listener,
            ime_listener_request,
        )?;

        let view = View::new(session.clone(), view_token, Some(String::from("Terminal")));
        let root_node = EntityNode::new(session.clone());
        view.add_child(&root_node);
        let pty = Pty::new()?;
        let pty_read_fd = pty.try_clone_fd()?;
        let pty_write_fd = pty.try_clone_fd()?;

        let view_controller = ViewController {
            face,
            session: session.clone(),
            view: view,
            root_node: root_node,
            image_cycler: HostImageCycler::new(session.clone()),
            metrics: None,
            logical_size: None,
            term: None,
            parser: Processor::new(),
            output_buffer: Vec::new(),
            pty: Rc::new(RefCell::new(pty)),
            pty_write_fd,
        };
        view_controller.setup_scene();
        view_controller.present();

        //TODO(MS-2376) Move all of the event handling logic into an event loop
        let view_controller = Arc::new(Mutex::new(view_controller));
        {
            let view_controller = view_controller.clone();
            fasync::spawn_local(
                async move {
                    // In order to keep the channel alive, we need to move ime_listener into this block.
                    // Otherwise it's unused, which closes the channel immediately.
                    let _dummy = ime_listener;
                    let mut stream = ime_client_listener_request.into_stream()?;
                    while let Some(request) = stream.try_next().await? {
                        match request {
                            InputMethodEditorClientRequest::DidUpdateState {
                                event: Some(event),
                                ..
                            } => {
                                ViewController::handle_input_event(
                                    &view_controller,
                                    *event
                                ).await
                                .unwrap_or_else(
                                    |e: failure::Error| {
                                        eprintln!("Unable to handle input event: {:?}", e)
                                    },
                                );
                            }
                            _ => (),
                        }
                    }
                    Ok(())
                }
                    .unwrap_or_else(|e: failure::Error| eprintln!("input listener error: {:?}", e)),
            );
        }
        {
            let view_controller = view_controller.clone();
            fasync::spawn_local(
                async move {
                    let mut stream = session_listener_request.into_stream()?;
                    while let Some(request) = stream.try_next().await? {
                        match request {
                            SessionListenerRequest::OnScenicEvent { events, control_handle: _ } => {
                                view_controller.lock().handle_session_events(events)
                            }
                            _ => (),
                        }
                    }
                    Ok(())
                }
                    .unwrap_or_else(|e: failure::Error| eprintln!("view listener error: {:?}", e)),
            );
        }
        {
            ViewController::spawn_io_loop(&view_controller, pty_read_fd);
        }
        Ok(view_controller)
    }

    fn spawn_io_loop(view_controller: &ViewControllerPtr, fd: File) {
        let view_controller = view_controller.clone();
        fasync::spawn_local(async move {
            {
                let vc_lock = view_controller.lock();
                let pty_cell = &vc_lock.pty.clone();
                drop(vc_lock);

                let mut pty = pty_cell.borrow_mut();

                // TODO(MS-2378) Wait until we have the actual window size before spawning.
                // This will require that we spawn the view controller in an async call which
                // requires a refactor of the app class.
                pty.spawn(WindowSize { width: 1000, height: 1000 }).await
                    .expect("failed to spawn pty");
            }
            let mut evented_fd = unsafe {
                // EventedFd::new() is unsafe because it can't guarantee the lifetime of
                // the file descriptor passed to it exceeds the lifetime of the EventedFd.
                // Since we're cloning the file when passing it in, the EventedFd
                // effectively owns that file descriptor and thus controls it's lifetime.
                fasync::net::EventedFd::new(fd).expect("failed to create evented_fd for io_loop")
            };
            let mut read_buf = [0u8, 32];
            loop {
                let bytes_read =
                    evented_fd.read(&mut read_buf).await.unwrap_or_else(|e: std::io::Error| {
                        eprintln!(
                            "failed to read bytes from io_loop, dropping current message: {:?}",
                            e
                        );
                        0
                    });

                if bytes_read > 0 {
                    view_controller.lock().handle_output(&read_buf[0..bytes_read]);
                }
            }
        });
    }

    fn setup_scene(&self) {
        self.root_node.resource().set_event_mask(gfx::METRICS_EVENT_MASK);
        self.root_node.add_child(self.image_cycler.node());
    }

    fn invalidate(&mut self) {
        self.begin_frame();
    }

    fn begin_frame(&mut self) {
        let (metrics, logical_size) = match (self.metrics.as_ref(), self.logical_size.as_ref()) {
            (Some(metrics), Some(logical_size)) => (metrics, logical_size),
            _ => return,
        };
        let physical_width = (logical_size.x * metrics.scale_x) as u32;
        let physical_height = (logical_size.y * metrics.scale_y) as u32;
        let stride = physical_width * 4;
        let info = images::ImageInfo {
            transform: images::Transform::Normal,
            width: physical_width,
            height: physical_height,
            stride,
            pixel_format: images::PixelFormat::Bgra8,
            color_space: images::ColorSpace::Srgb,
            tiling: images::Tiling::Linear,
            alpha_format: images::AlphaFormat::Opaque,
        };
        {
            let guard = self.image_cycler.acquire(info).expect("failed to allocate buffer");
            let mut face = self.face.lock();
            let mut canvas = Canvas::new(
                IntSize::new(physical_width as i32, physical_height as i32),
                MappingPixelSink::new(&guard.image().mapping()),
                stride,
                4,
            );
            let size = Size::new(14.0, 22.0);
            let mut font = FontDescription { face: &mut face, size: 20, baseline: 18 };

            let mut need_resize = false;

            let term = self.term.get_or_insert_with(|| {
                let term = Term::new(
                    &Config::default(),
                    SizeInfo {
                        width: physical_width as f32,
                        height: physical_height as f32,
                        cell_width: size.width as f32,
                        cell_height: size.height as f32,
                        padding_x: 0.,
                        padding_y: 0.,
                    },
                );

                need_resize = true;

                term
            });

            if need_resize {
                let pty_ref = self.pty.clone();
                let window_size =
                    WindowSize { width: logical_size.x as u32, height: logical_size.y as u32 };
                fasync::spawn_local(async move {
                    let pty = pty_ref.borrow();
                    pty.resize(window_size).await.unwrap_or_else(|e: failure::Error| {
                        eprintln!("failed to send resize message to pty: {:?}", e)
                    });
                });
            }

            if self.output_buffer.len() > 0 {
                for byte in &self.output_buffer {
                    self.parser.advance(term, *byte, &mut self.pty_write_fd);
                }
                self.output_buffer.clear();
            }
            for cell in term.renderable_cells(&Config::default(), None, true) {
                let mut buffer: [u8; 4] = [0, 0, 0, 0];
                canvas.fill_text_cells(
                    cell.c.encode_utf8(&mut buffer),
                    Point::new(size.width * cell.column.0 as f32, size.height * cell.line.0 as f32),
                    size,
                    &mut font,
                    &Paint {
                        fg: Color { r: cell.fg.r, g: cell.fg.g, b: cell.fg.b, a: 0xFF },
                        bg: Color { r: cell.bg.r, g: cell.bg.g, b: cell.bg.b, a: 0xFF },
                    },
                )
            }
        }

        let node = self.image_cycler.node();
        node.set_scale(1.0 / metrics.scale_x, 1.0 / metrics.scale_y, 1.0);
        node.set_translation(logical_size.x / 2.0, logical_size.y / 2.0, 0.0);
        self.present();
    }

    fn present(&self) {
        fasync::spawn(self.session.lock().present(0).map(|_| ()));
    }

    fn handle_session_events(&mut self, events: Vec<scenic::Event>) {
        events.iter().for_each(|event| match event {
            scenic::Event::Gfx(event) => match event {
                gfx::Event::Metrics(event) => {
                    assert!(event.node_id == self.root_node.id());
                    self.metrics = Some(gfx::Metrics { ..event.metrics });
                    self.invalidate();
                }
                gfx::Event::ViewPropertiesChanged(event) => {
                    assert!(event.view_id == self.view.id());
                    self.logical_size = Some(gfx::Vec3 {
                        x: event.properties.bounding_box.max.x
                            - event.properties.bounding_box.min.x,
                        y: event.properties.bounding_box.max.y
                            - event.properties.bounding_box.min.y,
                        z: event.properties.bounding_box.max.z
                            - event.properties.bounding_box.min.z,
                    });
                    self.invalidate();
                }
                _ => (),
            },
            _ => (),
        });
    }

    async fn handle_input_event(
        view_controller: &ViewControllerPtr,
        event: InputEvent,
    ) -> Result<(), Error> {
        let mut character: Option<char> = None;

        //TODO(2379) Update to handle more keys and use constants defined by
        // the fuchsia.ui.input library.
        if let InputEvent::Keyboard(event) = &event {
            if event.phase == KeyboardEventPhase::Pressed
                || event.phase == KeyboardEventPhase::Repeat
            {
                match (std::char::from_u32(event.code_point), event.hid_usage) {
                    (Some('\0'), 40) => {
                        // Enter key
                        character = Some('\n');
                    }
                    (Some('\0'), 42) => {
                        // Backspace key
                        character = Some('\x7f');
                    }
                    (Some('\0'), _) => {
                        // Some other non-printable key
                    }
                    (Some(codepoint), _) => {
                        // All printable keys
                        character = Some(codepoint);
                    }
                    (None, _) => {}
                }
            }
        }

        if let Some(character) = character {
            let mut buffer: [u8; 4] = [0; 4];
            let string = character.encode_utf8(&mut buffer);
            let mut evented_fd = unsafe {
                // EventedFd::new() is unsafe because it can't guarantee the lifetime of
                // the file descriptor passed to it exceeds the lifetime of the EventedFd.
                // Since we're cloning the file when passing it in, the EventedFd
                // effectively owns that file descriptor and thus controls it's lifetime.
                fasync::net::EventedFd::new(view_controller.lock().pty_write_fd.try_clone()?)?
            };

            evented_fd.write_all(string.as_bytes()).await
                .context("failed to write string to evented_fd")?;
        }

        Ok(())
    }

    fn handle_output(&mut self, output: &[u8]) {
        //TODO(MS-2377) update to not redraw on each call to handle_output
        self.output_buffer.extend(output);
        self.invalidate();
    }
}
