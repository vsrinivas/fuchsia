// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{FrameBufferPtr, MessageInternal, ViewMode},
    canvas::{Canvas, MappingPixelSink},
    geometry::{IntSize, Size},
    message::Message,
    scenic_utils::PresentationTime,
    view::strategies::{
        base::ViewStrategyPtr, framebuffer::FrameBufferViewStrategy, scenic::ScenicViewStrategy,
        scenic_canvas::ScenicCanvasViewStrategy,
    },
};
use anyhow::Error;
use fidl_fuchsia_ui_gfx::{self as gfx, Metrics, ViewProperties};
use fidl_fuchsia_ui_input::{
    FocusEvent, InputEvent, KeyboardEvent, PointerEvent, SetHardKeyboardDeliveryCmd,
};
use fidl_fuchsia_ui_views::ViewToken;
use fuchsia_async::{self as fasync, Interval};
use fuchsia_framebuffer::ImageId;
use fuchsia_scenic::{EntityNode, SessionPtr, View};
use fuchsia_zircon::{Duration, Event, Time};
use futures::{channel::mpsc::UnboundedSender, StreamExt, TryFutureExt};
use std::{cell::RefCell, collections::BTreeMap};

mod strategies;

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
    app_sender: UnboundedSender<MessageInternal>,
}

impl ScenicResources {
    fn new(
        session: &SessionPtr,
        view_token: ViewToken,
        app_sender: UnboundedSender<MessageInternal>,
    ) -> ScenicResources {
        let view = View::new(session.clone(), view_token, Some(String::from("Carnelian View")));
        let root_node = EntityNode::new(session.clone());
        root_node.resource().set_event_mask(gfx::METRICS_EVENT_MASK);
        view.add_child(&root_node);

        session.lock().enqueue(fidl_fuchsia_ui_scenic::Command::Input(
            fidl_fuchsia_ui_input::Command::SetHardKeyboardDelivery(SetHardKeyboardDeliveryCmd {
                delivery_request: true,
            }),
        ));
        ScenicResources {
            view,
            root_node,
            session: session.clone(),
            pending_present_count: 0,
            app_sender,
        }
    }
}

fn scenic_present(scenic_resources: &mut ScenicResources, key: ViewKey) {
    if scenic_resources.pending_present_count < 3 {
        let app_sender = scenic_resources.app_sender.clone();
        fasync::spawn_local(
            scenic_resources
                .session
                .lock()
                .present(0)
                .map_ok(move |_| {
                    app_sender
                        .unbounded_send(MessageInternal::ScenicPresentDone(key))
                        .expect("unbounded_send");
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

pub(crate) type Canvases = BTreeMap<ImageId, RefCell<Canvas<MappingPixelSink>>>;

pub(crate) struct ViewDetails {
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
    app_sender: UnboundedSender<MessageInternal>,
}

impl ViewController {
    pub(crate) async fn new(
        key: ViewKey,
        view_token: ViewToken,
        mode: ViewMode,
        session: SessionPtr,
        mut view_assistant: ViewAssistantPtr,
        app_sender: UnboundedSender<MessageInternal>,
    ) -> Result<ViewController, Error> {
        let strategy = if mode == ViewMode::Canvas {
            ScenicCanvasViewStrategy::new(&session, view_token, app_sender.clone()).await?
        } else {
            ScenicViewStrategy::new(&session, view_token, app_sender.clone())
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
            app_sender,
        };

        view_controller
            .strategy
            .setup(&view_controller.make_view_details(), &mut view_controller.assistant);

        Ok(view_controller)
    }

    /// new_with_frame_buffer
    pub(crate) fn new_with_frame_buffer(
        key: ViewKey,
        size: IntSize,
        pixel_size: u32,
        pixel_format: fuchsia_framebuffer::PixelFormat,
        stride: u32,
        mut view_assistant: ViewAssistantPtr,
        app_sender: UnboundedSender<MessageInternal>,
        frame_buffer: FrameBufferPtr,
        signals_wait_event: bool,
    ) -> Result<ViewController, Error> {
        let strategy = FrameBufferViewStrategy::new(
            key,
            &size,
            pixel_size,
            pixel_format,
            stride,
            app_sender.clone(),
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
            app_sender: app_sender.clone(),
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
        self.app_sender.unbounded_send(MessageInternal::Update(self.key)).expect("unbounded_send");
    }

    pub(crate) async fn update_async(&mut self) {
        // Recompute our logical size based on the provided physical size and screen metrics.
        self.logical_size = Size::new(
            self.physical_size.width * self.metrics.width,
            self.physical_size.height * self.metrics.height,
        );

        self.strategy.update(&self.make_view_details(), &mut self.assistant).await;
        self.present();
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
        let app_sender = self.app_sender.clone();
        let f = timer
            .map(move |_| {
                app_sender.unbounded_send(MessageInternal::Update(key)).expect("unbounded_send");
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
            }
        } else {
            self.assistant.handle_message(msg);
        }
    }

    pub(crate) fn image_freed(&mut self, image_id: u64, collection_id: u32) {
        self.strategy.image_freed(image_id, collection_id);
    }

    pub fn handle_vsync_parameters_changed(&mut self, phase: Time, interval: Duration) {
        self.strategy.handle_vsync_parameters_changed(phase, interval);
    }
}
