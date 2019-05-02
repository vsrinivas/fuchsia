use carnelian::{
    Canvas, Color, FontDescription, FontFace, IntSize, MappingPixelSink, Paint, Point, Size,
};
use failure::Error;
use fidl::endpoints::{create_endpoints, ServerEnd};
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
use futures::{FutureExt, TryFutureExt, TryStreamExt};
use parking_lot::Mutex;
use std::sync::Arc;
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
        };
        view_controller.setup_scene();
        view_controller.present();

        let view_controller = Arc::new(Mutex::new(view_controller));
        {
            let view_controller = view_controller.clone();
            fasync::spawn(
                async move {
                    // In order to keep the channel alive, we need to move ime_listener into this block.
                    // Otherwise it's unused, which closes the channel immediately.
                    let _dummy = ime_listener;
                    let mut stream = ime_client_listener_request.into_stream()?;
                    while let Some(request) = await!(stream.try_next())? {
                        match request {
                            InputMethodEditorClientRequest::DidUpdateState {
                                event: Some(event),
                                ..
                            } => view_controller.lock().handle_input_event(*event),
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
            fasync::spawn(
                async move {
                    let mut stream = session_listener_request.into_stream()?;
                    while let Some(request) = await!(stream.try_next())? {
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
        Ok(view_controller)
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
            let mut canvas = Canvas::<MappingPixelSink>::new(
                IntSize::new(physical_width as i32, physical_height as i32),
                guard.image().mapping().clone(),
                stride,
                4,
            );
            let size = Size::new(14.0, 22.0);
            let mut font = FontDescription { face: &mut face, size: 20, baseline: 18 };
            let parser = &mut self.parser;
            let term = self.term.get_or_insert_with(|| {
                let mut term = Term::new(
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
                for byte in "$ echo \"\u{001b}[31mhello, world!\u{001b}[0m\"".as_bytes() {
                    parser.advance(&mut term, *byte, &mut ::std::io::sink());
                }
                term
            });
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

    fn handle_input_event(&mut self, event: InputEvent) {
        if let (Some(term), InputEvent::Keyboard(event)) = (&mut self.term, &event) {
            if event.phase == KeyboardEventPhase::Pressed
                || event.phase == KeyboardEventPhase::Repeat
            {
                if let Some(c) = std::char::from_u32(event.code_point) {
                    if c != '\0' {
                        // The API that parses escape sequences (the vte library) takes as input a
                        // stream of bytes  in utf8, rather than taking a stream of codepoints. Thus,
                        // we need to  convert each codepoint into a ut8 byte stream, which it will
                        // then convert back to codepoints.
                        // There is an issue on the vte github to address this:
                        // https://github.com/jwilm/vte/issues/19
                        let mut buffer: [u8; 4] = [0, 0, 0, 0];
                        c.encode_utf8(&mut buffer);
                        for i in 0..c.len_utf8() {
                            self.parser.advance(term, buffer[i], &mut ::std::io::sink());
                        }
                        self.invalidate();
                    }
                }
            }
        }
    }
}
