// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{App, FrameBufferPtr, TestSender, ViewMode},
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
use fuchsia_framebuffer::{FrameSet, ImageId};
use fuchsia_scenic::{EntityNode, HostImageCycler, SessionPtr, View};
use fuchsia_zircon::{self as zx, ClockId, Duration, Event, HandleBased, Time};
use futures::{channel::mpsc::unbounded, StreamExt, TryFutureExt};
use std::{cell::RefCell, collections::BTreeMap};

/// enum that defines all messages sent with `App::send_message` that
/// the view struct will understand and process.
pub enum ViewMessages {
    /// Message that requests that a view redraw itself.
    Update,
    /// Sent when a scenic present has completed
    ScenicPresentDone,
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
    /// When running in frame buffer mode, the number of buffers in
    /// the buffer collection
    pub buffer_count: Option<usize>,
    /// When running in frame buffer mode, the the event to signal
    /// to indicate that the layer image can swap to the currently
    /// set frame
    pub wait_event: Option<&'a Event>,

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
    fn setup(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error>;

    /// This method is called when a view controller has been asked to update the view.
    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error>;

    /// This method is called when input events come from scenic to this view.
    fn handle_input_event(
        &mut self,
        context: &mut ViewAssistantContext<'_>,
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
        _: &mut ViewAssistantContext<'_>,
        _: &PointerEvent,
    ) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called when keyboard events come from scenic to this view.
    fn handle_keyboard_event(
        &mut self,
        _: &mut ViewAssistantContext<'_>,
        _: &KeyboardEvent,
    ) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called when focus events come from scenic to this view.
    fn handle_focus_event(
        &mut self,
        _: &mut ViewAssistantContext<'_>,
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

    /// Pixel format for image pipe mode
    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat {
        fuchsia_framebuffer::PixelFormat::Argb8888
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
    pending_present_count: usize,
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
        ScenicResources { view, root_node, session: session.clone(), pending_present_count: 0 }
    }
}

struct ScenicCanvasResources {
    image_cycler: HostImageCycler,
}

trait ViewStrategy {
    fn setup(&mut self, _view_details: &ViewDetails, _view_assistant: &mut ViewAssistantPtr);
    fn update(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr);
    fn present(&mut self, view_details: &ViewDetails);
    fn present_done(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
    ) {
    }
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
    ) -> Vec<Message>;

    fn image_freed(&mut self, _image_id: u64) {}
}

type ViewStrategyPtr = Box<dyn ViewStrategy>;

struct ScenicViewStrategy {
    scenic_resources: ScenicResources,
}

fn scenic_present(scenic_resources: &mut ScenicResources, key: ViewKey) {
    if scenic_resources.pending_present_count < 3 {
        fasync::spawn_local(
            scenic_resources
                .session
                .lock()
                .present(0)
                .map_ok(move |_| {
                    App::with(|app| {
                        app.queue_message(key, make_message(ViewMessages::ScenicPresentDone));
                    })
                })
                .unwrap_or_else(|e| panic!("present error: {:?}", e)),
        );
        scenic_resources.pending_present_count += 1;
    }
}

fn scenic_present_done(scenic_resources: &mut ScenicResources) {
    assert_ne!(scenic_resources.pending_present_count, 0);
    scenic_resources.pending_present_count -= 1;
}

impl ScenicViewStrategy {
    fn new(session: &SessionPtr, view_token: ViewToken) -> ViewStrategyPtr {
        let scenic_resources = ScenicResources::new(session, view_token);
        Box::new(ScenicViewStrategy { scenic_resources })
    }

    fn make_view_assistant_context(&self, view_details: &ViewDetails) -> ViewAssistantContext<'_> {
        ViewAssistantContext {
            key: view_details.key,
            logical_size: view_details.logical_size,
            size: view_details.physical_size,
            metrics: view_details.metrics,
            presentation_time: Time::get(ClockId::Monotonic),
            messages: Vec::new(),
            scenic_resources: Some(&self.scenic_resources),
            canvas: None,
            buffer_count: None,
            wait_event: None,
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

    fn present(&mut self, view_details: &ViewDetails) {
        scenic_present(&mut self.scenic_resources, view_details.key);
    }

    fn present_done(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
    ) {
        scenic_present_done(&mut self.scenic_resources);
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
        canvas: Option<&'a RefCell<Canvas<MappingPixelSink>>>,
    ) -> ViewAssistantContext<'a> {
        ViewAssistantContext {
            key: view_details.key,
            logical_size: view_details.logical_size,
            size: view_details.physical_size,
            metrics: view_details.metrics,
            presentation_time: Time::get(ClockId::Monotonic),
            messages: Vec::new(),
            scenic_resources: None,
            canvas: canvas,
            buffer_count: None,
            wait_event: None,
        }
    }
}

impl ViewStrategy for ScenicCanvasViewStrategy {
    fn setup(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        let canvas_context =
            ScenicCanvasViewStrategy::make_view_assistant_context(view_details, None);
        view_assistant.setup(&canvas_context).unwrap_or_else(|e| panic!("Setup error: {:?}", e));
    }

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
                0,
                0,
            ));

            let canvas_context =
                ScenicCanvasViewStrategy::make_view_assistant_context(view_details, Some(&canvas));
            view_assistant
                .update(&canvas_context)
                .unwrap_or_else(|e| panic!("Update error: {:?}", e));
        }
    }

    fn present(&mut self, view_details: &ViewDetails) {
        scenic_present(&mut self.scenic_resources, view_details.key);
    }

    fn present_done(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
    ) {
        scenic_present_done(&mut self.scenic_resources);
    }

    fn handle_input_event(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        event: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Vec<Message> {
        let mut canvas_context =
            ScenicCanvasViewStrategy::make_view_assistant_context(view_details, None);
        view_assistant
            .handle_input_event(&mut canvas_context, &event)
            .unwrap_or_else(|e| eprintln!("handle_event: {:?}", e));

        canvas_context.messages
    }
}

type Canvases = BTreeMap<ImageId, RefCell<Canvas<MappingPixelSink>>>;
type WaitEvents = BTreeMap<ImageId, Event>;

struct FrameBufferViewStrategy {
    frame_buffer: FrameBufferPtr,
    canvases: Canvases,
    frame_set: FrameSet,
    image_sender: futures::channel::mpsc::UnboundedSender<u64>,
    wait_events: WaitEvents,
    signals_wait_event: bool,
}

impl FrameBufferViewStrategy {
    fn new(
        size: &IntSize,
        pixel_size: u32,
        _pixel_format: fuchsia_framebuffer::PixelFormat,
        stride: u32,
        frame_buffer: FrameBufferPtr,
        signals_wait_event: bool,
    ) -> ViewStrategyPtr {
        let mut fb = frame_buffer.borrow_mut();
        let mut canvases: Canvases = Canvases::new();
        let mut wait_events: WaitEvents = WaitEvents::new();
        let image_ids = fb.get_image_ids();
        image_ids.iter().for_each(|image_id| {
            let frame = fb.get_frame_mut(*image_id);
            let canvas = RefCell::new(Canvas::new(
                *size,
                MappingPixelSink::new(&frame.mapping),
                stride,
                pixel_size,
                frame.image_id,
                frame.image_index,
            ));
            canvases.insert(frame.image_id, canvas);
            if signals_wait_event {
                let wait_event = frame
                    .wait_event
                    .duplicate_handle(zx::Rights::SAME_RIGHTS)
                    .expect("duplicate_handle");
                wait_events.insert(frame.image_id, wait_event);
            }
        });
        let (image_sender, mut image_receiver) = unbounded::<u64>();
        // wait for events from the image freed fence to know when an
        // image can prepared.
        fasync::spawn_local(async move {
            while let Some(image_id) = image_receiver.next().await {
                App::with(|app| {
                    app.image_freed(image_id);
                })
            }
        });
        let frame_set = FrameSet::new(image_ids);
        Box::new(FrameBufferViewStrategy {
            canvases,
            frame_buffer: frame_buffer.clone(),
            frame_set: frame_set,
            image_sender: image_sender,
            wait_events,
            signals_wait_event,
        })
    }

    fn make_context(
        &mut self,
        view_details: &ViewDetails,
        image_id: ImageId,
    ) -> ViewAssistantContext<'_> {
        let wait_event = if self.signals_wait_event {
            let stored_wait_event = self.wait_events.get(&image_id).expect("wait event");
            Some(stored_wait_event)
        } else {
            None
        };

        ViewAssistantContext {
            key: view_details.key,
            logical_size: view_details.logical_size,
            size: view_details.physical_size,
            metrics: view_details.metrics,
            presentation_time: Time::get(ClockId::Monotonic),
            messages: Vec::new(),
            scenic_resources: None,
            canvas: Some(
                &self.canvases.get(&image_id).expect("failed to get canvas in make_context"),
            ),
            buffer_count: Some(self.frame_buffer.borrow().get_frame_count()),
            wait_event: wait_event,
        }
    }
}

impl ViewStrategy for FrameBufferViewStrategy {
    fn setup(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        if let Some(available) = self.frame_set.get_available_image() {
            let framebuffer_context = self.make_context(view_details, available);
            view_assistant
                .setup(&framebuffer_context)
                .unwrap_or_else(|e| panic!("Setup error: {:?}", e));
            self.frame_set.return_image(available);
        }
    }

    fn update(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        if let Some(available) = self.frame_set.get_available_image() {
            let framebuffer_context = self.make_context(view_details, available);
            view_assistant
                .update(&framebuffer_context)
                .unwrap_or_else(|e| panic!("Update error: {:?}", e));
            self.frame_set.mark_prepared(available);
        }
    }

    fn present(&mut self, _view_details: &ViewDetails) {
        if let Some(prepared) = self.frame_set.prepared {
            let mut fb = self.frame_buffer.borrow_mut();
            fb.present_frame(prepared, Some(self.image_sender.clone()), !self.signals_wait_event)
                .unwrap_or_else(|e| panic!("Present error: {:?}", e));
            self.frame_set.mark_presented(prepared);
        }
    }

    fn handle_input_event(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
        _event: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Vec<Message> {
        panic!("Not yet implemented");
    }

    fn image_freed(&mut self, image_id: u64) {
        self.frame_set.mark_done_presenting(image_id);
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
        stride: u32,
        mut view_assistant: ViewAssistantPtr,
        test_sender: Option<TestSender>,
        frame_buffer: FrameBufferPtr,
        signals_wait_event: bool,
    ) -> Result<ViewController, Error> {
        let strategy = FrameBufferViewStrategy::new(
            &size,
            pixel_size,
            pixel_format,
            stride,
            frame_buffer,
            signals_wait_event,
        );
        let initial_animation_mode = view_assistant.initial_animation_mode();
        let mut view_controller = ViewController {
            key,
            metrics: Size::new(1.0, 1.0),
            physical_size: size.to_f32(),
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
        self.strategy.present(&self.make_view_details());
    }

    pub(crate) fn update(&mut self) {
        // Recompute our logical size based on the provided physical size and screen metrics.
        self.logical_size = Size::new(
            self.physical_size.width * self.metrics.width,
            self.physical_size.height * self.metrics.height,
        );

        self.strategy.update(&self.make_view_details(), &mut self.assistant);
        self.present();
        if let Some(sender) = self.test_sender.as_ref() {
            sender
                .unbounded_send(Ok(()))
                .unwrap_or_else(|err| println!("sending failed {:?}", err));
        }
    }

    pub(crate) fn present_done(&mut self) {
        self.strategy.present_done(&self.make_view_details(), &mut self.assistant);
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
                ViewMessages::ScenicPresentDone => {
                    self.present_done();
                }
            }
        } else {
            self.assistant.handle_message(msg);
        }
    }

    pub(crate) fn image_freed(&mut self, image_id: u64) {
        self.strategy.image_freed(image_id);
    }
}
