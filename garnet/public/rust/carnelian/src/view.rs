// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{App, TestSender, ViewMode},
    canvas::{Canvas, MappingPixelSink},
    geometry::{IntSize, Size},
    message::{make_message, Message},
    scenic_utils::PresentationTime,
};
use failure::Error;
use fidl_fuchsia_images as images;
use fidl_fuchsia_ui_gfx::{self as gfx, Metrics, ViewProperties};
use fidl_fuchsia_ui_input::{
    FocusEvent, InputEvent, KeyboardEvent, PointerEvent, SetHardKeyboardDeliveryCmd,
};
use fidl_fuchsia_ui_views::ViewToken;
use fuchsia_async::{self as fasync, Interval};
use fuchsia_scenic::{EntityNode, HostImageCycler, SessionPtr, View};
use fuchsia_zircon::Duration;
use fuchsia_zircon::{ClockId, Time};
use futures::{StreamExt, TryFutureExt};
use mapped_vmo::Mapping;
use std::{cell::RefCell, sync::Arc};

/// enum that defines all messages sent with `App::send_message` that
/// the view struct will understand and process.
pub enum ViewMessages {
    /// Message that requests that a view redraw itself.
    Update,
}

/// enum that defines the animation behavior of the view
#[derive(Clone, Copy, Debug)]
pub enum AnimationMode {
    /// No automatic update, only on explicit calls to update
    None,

    /// Call update in preparation for every frame
    EveryFrame,

    /// Call update periodically based on duration
    RefreshRate(Duration),
}

/// parameter struct passed to setup and update trait methods.
pub struct ViewAssistantContext<'a> {
    /// A unique key representing this view.
    pub key: ViewKey,
    /// The size taking screen density into account.
    pub logical_size: Size,
    /// The actual number of pixels in the view.
    pub size: Size,
    /// The factor between logical size and size.
    pub metrics: Size,
    /// For update, the time this will be presented.
    pub presentation_time: PresentationTime,
    /// For views in Scenic mode, this will contain resources for
    /// interacting with Scenic.
    pub scenic_resources: Option<&'a ScenicResources>,
    /// For views in canvas mode, this will contain a canvas
    /// to be used for drawing.
    pub canvas: Option<&'a RefCell<Canvas<MappingPixelSink>>>,

    messages: Vec<Message>,
}

impl<'a> ViewAssistantContext<'a> {
    /// Queue up a message for delivery
    pub fn queue_message(&mut self, message: Message) {
        self.messages.push(message);
    }

    /// Get the root node for scenic based apps
    pub fn root_node(&self) -> &EntityNode {
        &self.scenic_resources.as_ref().unwrap().root_node
    }

    /// Get the session for scenic based apps
    pub fn session(&self) -> &SessionPtr {
        &self.scenic_resources.as_ref().unwrap().session
    }
}

/// Trait that allows mod developers to customize the behavior of view controllers.
pub trait ViewAssistant {
    /// This method is called once when a view is created. It is a good point to create scenic
    /// commands that apply throughout the lifetime of the view.
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error>;

    /// This method is called when a view controller has been asked to update the view.
    fn update(&mut self, context: &ViewAssistantContext) -> Result<(), Error>;

    /// This method is called when input events come from scenic to this view.
    fn handle_input_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &InputEvent,
    ) -> Result<(), Error> {
        match event {
            InputEvent::Pointer(pointer_event) => {
                self.handle_pointer_event(context, &pointer_event)
            }
            InputEvent::Keyboard(keyboard_event) => {
                self.handle_keyboard_event(context, &keyboard_event)
            }
            InputEvent::Focus(focus_event) => self.handle_focus_event(context, &focus_event),
        }
    }

    /// This method is called when input events come from scenic to this view.
    fn handle_pointer_event(
        &mut self,
        _: &mut ViewAssistantContext,
        _: &PointerEvent,
    ) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called when keyboard events come from scenic to this view.
    fn handle_keyboard_event(
        &mut self,
        _: &mut ViewAssistantContext,
        _: &KeyboardEvent,
    ) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called when focus events come from scenic to this view.
    fn handle_focus_event(
        &mut self,
        _: &mut ViewAssistantContext,
        _: &FocusEvent,
    ) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called when `App::send_message` is called with the associated
    /// view controller's `ViewKey` and the view controller does not handle the message.
    fn handle_message(&mut self, _message: Message) {}

    /// Initial animation mode for view
    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::None;
    }
}

/// Reference to a view assistant. _This type is likely to change in the future so
/// using this type alias might make for easier forward migration._
pub type ViewAssistantPtr = Box<dyn ViewAssistant>;

/// Key identifying a view.
pub type ViewKey = u64;

pub struct ScenicResources {
    #[allow(unused)]
    view: View,
    root_node: EntityNode,
    session: SessionPtr,
}

impl ScenicResources {
    fn new(session: &SessionPtr, view_token: ViewToken) -> ScenicResources {
        let view = View::new(session.clone(), view_token, Some(String::from("Carnelian View")));
        let root_node = EntityNode::new(session.clone());
        root_node.resource().set_event_mask(gfx::METRICS_EVENT_MASK);
        view.add_child(&root_node);

        session.lock().enqueue(fidl_fuchsia_ui_scenic::Command::Input(
            fidl_fuchsia_ui_input::Command::SetHardKeyboardDelivery(SetHardKeyboardDeliveryCmd {
                delivery_request: true,
            }),
        ));
        ScenicResources { view, root_node, session: session.clone() }
    }
}

struct ScenicCanvasResources {
    image_cycler: HostImageCycler,
}

struct FramebufferResources {
    canvas: RefCell<Canvas<MappingPixelSink>>,
}

trait ViewStrategy {
    fn setup(&mut self, _view_details: &ViewDetails, _view_assistant: &mut ViewAssistantPtr) {}
    fn update(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr);
    fn present(&mut self) {}
    fn validate_root_node_id(&self, _: u32) -> bool {
        true
    }
    fn validate_view_id(&self, _: u32) -> bool {
        true
    }

    fn handle_input_event(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
        _: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Vec<Message> {
        Vec::new()
    }
}

type ViewStrategyPtr = Box<dyn ViewStrategy>;

struct ScenicViewStrategy {
    scenic_resources: ScenicResources,
}

fn scenic_present(scenic_resources: &ScenicResources) {
    fasync::spawn_local(
        scenic_resources
            .session
            .lock()
            .present(0)
            .map_ok(|_| ())
            .unwrap_or_else(|e| panic!("present error: {:?}", e)),
    );
}

impl ScenicViewStrategy {
    fn new(session: &SessionPtr, view_token: ViewToken) -> ViewStrategyPtr {
        let scenic_resources = ScenicResources::new(session, view_token);
        Box::new(ScenicViewStrategy { scenic_resources })
    }

    fn make_view_assistant_context(&self, view_details: &ViewDetails) -> ViewAssistantContext {
        ViewAssistantContext {
            key: view_details.key,
            logical_size: view_details.logical_size,
            size: view_details.physical_size,
            metrics: view_details.metrics,
            presentation_time: Time::get(ClockId::Monotonic),
            messages: Vec::new(),
            scenic_resources: Some(&self.scenic_resources),
            canvas: None,
        }
    }
}

impl ViewStrategy for ScenicViewStrategy {
    fn setup(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        let context = self.make_view_assistant_context(view_details);
        view_assistant.setup(&context).unwrap_or_else(|e| panic!("Setup error: {:?}", e));
    }

    fn update(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        let context = self.make_view_assistant_context(view_details);
        view_assistant.update(&context).unwrap_or_else(|e| panic!("Update error: {:?}", e));
    }

    fn present(&mut self) {
        scenic_present(&self.scenic_resources);
    }

    fn handle_input_event(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        event: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Vec<Message> {
        let mut context = self.make_view_assistant_context(view_details);

        view_assistant
            .handle_input_event(&mut context, &event)
            .unwrap_or_else(|e| eprintln!("handle_event: {:?}", e));

        context.messages
    }
}

struct ScenicCanvasViewStrategy {
    #[allow(unused)]
    scenic_resources: ScenicResources,
    canvas_resources: ScenicCanvasResources,
}

impl ScenicCanvasViewStrategy {
    fn new(session: &SessionPtr, view_token: ViewToken) -> ViewStrategyPtr {
        let scenic_resources = ScenicResources::new(session, view_token);
        let image_cycler = HostImageCycler::new(session.clone());
        scenic_resources.root_node.add_child(&image_cycler.node());
        let canvas_resources = ScenicCanvasResources { image_cycler };
        let strat = ScenicCanvasViewStrategy { scenic_resources, canvas_resources };
        Box::new(strat)
    }

    fn make_view_assistant_context<'a>(
        view_details: &ViewDetails,
        canvas: &'a RefCell<Canvas<MappingPixelSink>>,
    ) -> ViewAssistantContext<'a> {
        ViewAssistantContext {
            key: view_details.key,
            logical_size: view_details.logical_size,
            size: view_details.physical_size,
            metrics: view_details.metrics,
            presentation_time: Time::get(ClockId::Monotonic),
            messages: Vec::new(),
            scenic_resources: None,
            canvas: Some(canvas),
        }
    }
}

impl ViewStrategy for ScenicCanvasViewStrategy {
    fn update(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        let size = view_details.physical_size.floor().to_u32();
        if size.width > 0 && size.height > 0 {
            let center_x = view_details.physical_size.width * 0.5;
            let center_y = view_details.physical_size.height * 0.5;
            let image_cycler = &mut self.canvas_resources.image_cycler;
            image_cycler.node().set_translation(center_x, center_y, -0.1);
            let stride = size.width * 4;

            // Create a description of this pixel buffer that
            // Scenic can understand.
            let info = images::ImageInfo {
                transform: images::Transform::Normal,
                width: size.width,
                height: size.height,
                stride: stride,
                pixel_format: images::PixelFormat::Bgra8,
                color_space: images::ColorSpace::Srgb,
                tiling: images::Tiling::Linear,
                alpha_format: images::AlphaFormat::Opaque,
            };

            // Grab an image buffer from the cycler
            let guard = image_cycler.acquire(info).expect("failed to allocate buffer");
            // Create a canvas to render into the buffer
            let canvas = RefCell::new(Canvas::new(
                view_details.physical_size.to_i32(),
                MappingPixelSink::new(&guard.image().mapping()),
                stride,
                4,
            ));

            let canvas_context =
                ScenicCanvasViewStrategy::make_view_assistant_context(view_details, &canvas);
            view_assistant
                .update(&canvas_context)
                .unwrap_or_else(|e| panic!("Update error: {:?}", e));
        }
    }

    fn present(&mut self) {
        scenic_present(&self.scenic_resources);
    }
}

struct FrameBufferViewStrategy {
    framebuffer_resources: FramebufferResources,
}

impl FrameBufferViewStrategy {
    fn new(
        size: &IntSize,
        mapping: &Arc<Mapping>,
        pixel_size: u32,
        _pixel_format: fuchsia_framebuffer::PixelFormat,
    ) -> ViewStrategyPtr {
        let stride = size.width as u32 * pixel_size;
        let canvas = Canvas::new(*size, MappingPixelSink::new(mapping), stride, pixel_size);
        let framebuffer_resources = FramebufferResources { canvas: RefCell::new(canvas) };
        Box::new(FrameBufferViewStrategy { framebuffer_resources })
    }
}

impl ViewStrategy for FrameBufferViewStrategy {
    fn update(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        let canvas_context = ViewAssistantContext {
            key: view_details.key,
            logical_size: view_details.logical_size,
            size: view_details.physical_size,
            metrics: view_details.metrics,
            presentation_time: Time::get(ClockId::Monotonic),
            messages: Vec::new(),
            scenic_resources: None,
            canvas: Some(&self.framebuffer_resources.canvas),
        };
        view_assistant.update(&canvas_context).unwrap_or_else(|e| panic!("Update error: {:?}", e));
    }
}

struct ViewDetails {
    key: ViewKey,
    metrics: Size,
    physical_size: Size,
    logical_size: Size,
    #[allow(unused)]
    animation_mode: AnimationMode,
}

/// This struct takes care of all the boilerplate needed for implementing a Fuchsia
/// view, forwarding the interesting implementation points to a struct implementing
/// the `ViewAssistant` trait.
pub struct ViewController {
    key: ViewKey,
    assistant: ViewAssistantPtr,
    metrics: Size,
    physical_size: Size,
    logical_size: Size,
    animation_mode: AnimationMode,
    strategy: ViewStrategyPtr,
    test_sender: Option<TestSender>,
}

impl ViewController {
    pub(crate) fn new(
        key: ViewKey,
        view_token: ViewToken,
        mode: ViewMode,
        session: SessionPtr,
        mut view_assistant: ViewAssistantPtr,
        test_sender: Option<TestSender>,
    ) -> Result<ViewController, Error> {
        let strategy = if mode == ViewMode::Canvas {
            ScenicCanvasViewStrategy::new(&session, view_token)
        } else {
            ScenicViewStrategy::new(&session, view_token)
        };

        let initial_animation_mode = view_assistant.initial_animation_mode();

        let mut view_controller = ViewController {
            key,
            metrics: Size::zero(),
            physical_size: Size::zero(),
            logical_size: Size::zero(),
            animation_mode: initial_animation_mode,
            assistant: view_assistant,
            strategy,
            test_sender: test_sender,
        };

        view_controller
            .strategy
            .setup(&view_controller.make_view_details(), &mut view_controller.assistant);

        Ok(view_controller)
    }

    /// new_with_frame_buffer
    pub fn new_with_frame_buffer(
        key: ViewKey,
        size: IntSize,
        pixel_size: u32,
        pixel_format: fuchsia_framebuffer::PixelFormat,
        mapping: Arc<Mapping>,
        mut view_assistant: ViewAssistantPtr,
    ) -> Result<ViewController, Error> {
        let strategy = FrameBufferViewStrategy::new(&size, &mapping, pixel_size, pixel_format);
        let initial_animation_mode = view_assistant.initial_animation_mode();
        let mut view_controller = ViewController {
            key,
            metrics: Size::new(1.0, 1.0),
            physical_size: size.to_f32(),
            logical_size: Size::zero(),
            animation_mode: initial_animation_mode,
            assistant: view_assistant,
            strategy,
            test_sender: None,
        };

        view_controller
            .strategy
            .setup(&view_controller.make_view_details(), &mut view_controller.assistant);

        Ok(view_controller)
    }

    fn make_view_details(&self) -> ViewDetails {
        ViewDetails {
            key: self.key,
            metrics: self.metrics,
            physical_size: self.physical_size,
            logical_size: self.logical_size,
            animation_mode: self.animation_mode,
        }
    }

    /// Informs Scenic of any changes made to this View's |Session|.
    pub fn present(&mut self) {
        self.strategy.present();
    }

    fn update(&mut self) {
        // Recompute our logical size based on the provided physical size and screen metrics.
        self.logical_size = Size::new(
            self.physical_size.width * self.metrics.width,
            self.physical_size.height * self.metrics.height,
        );

        self.strategy.update(&self.make_view_details(), &mut self.assistant);
        self.present();
        if let Some(sender) = self.test_sender.take() {
            sender.send(Ok(())).unwrap_or_else(|err| println!("sending failed {:?}", err));
        }
    }

    fn handle_metrics_changed(&mut self, metrics: &Metrics) {
        self.metrics = Size::new(metrics.scale_x, metrics.scale_y);
        self.update();
    }

    fn handle_view_properties_changed(&mut self, properties: &ViewProperties) {
        self.physical_size = Size::new(
            properties.bounding_box.max.x - properties.bounding_box.min.x,
            properties.bounding_box.max.y - properties.bounding_box.min.y,
        );
        self.update();
    }

    fn setup_timer(&self, duration: Duration) {
        let key = self.key;
        let timer = Interval::new(duration);
        let f = timer
            .map(move |_| {
                App::with(|app| {
                    app.queue_message(key, make_message(ViewMessages::Update));
                });
            })
            .collect::<()>();
        fasync::spawn_local(f);
    }

    pub(crate) fn setup_animation_mode(&mut self) {
        match self.animation_mode {
            AnimationMode::None => {}
            AnimationMode::EveryFrame => {
                self.setup_timer(Duration::from_millis(10));
            }
            AnimationMode::RefreshRate(duration) => {
                self.setup_timer(duration);
            }
        }
    }

    /// Handler for Events on this ViewController's Session.
    pub fn handle_session_events(&mut self, events: Vec<fidl_fuchsia_ui_scenic::Event>) {
        events.iter().for_each(|event| match event {
            fidl_fuchsia_ui_scenic::Event::Gfx(event) => match event {
                fidl_fuchsia_ui_gfx::Event::Metrics(event) => {
                    assert!(self.strategy.validate_root_node_id(event.node_id));
                    self.handle_metrics_changed(&event.metrics);
                }
                fidl_fuchsia_ui_gfx::Event::ViewPropertiesChanged(event) => {
                    assert!(self.strategy.validate_view_id(event.view_id));
                    self.handle_view_properties_changed(&event.properties);
                }
                _ => (),
            },
            fidl_fuchsia_ui_scenic::Event::Input(event) => {
                let messages = self.strategy.handle_input_event(
                    &self.make_view_details(),
                    &mut self.assistant,
                    &event,
                );
                for msg in messages {
                    self.send_message(msg);
                }
                self.update();
            }
            _ => (),
        });
    }

    /// This method sends an arbitrary message to this view. If it is not
    /// handled directly by `ViewController::send_message` it will be forwarded
    /// to the view assistant.
    pub fn send_message(&mut self, msg: Message) {
        if let Some(view_msg) = msg.downcast_ref::<ViewMessages>() {
            match view_msg {
                ViewMessages::Update => {
                    self.update();
                }
            }
        } else {
            self.assistant.handle_message(msg);
        }
    }
}
