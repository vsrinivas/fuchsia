// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{FrameBufferPtr, MessageInternal, RenderOptions},
    geometry::{IntSize, Size},
    input::{self},
    message::Message,
    render::Context,
    view::strategies::{
        base::ViewStrategyPtr, framebuffer::FrameBufferViewStrategy, scenic::ScenicViewStrategy,
    },
};
use anyhow::Error;
use fidl_fuchsia_ui_gfx::{self as gfx, Metrics, ViewProperties};
use fidl_fuchsia_ui_input::SetHardKeyboardDeliveryCmd;
use fidl_fuchsia_ui_views::{ViewRef, ViewRefControl, ViewToken};
use fuchsia_async::{self as fasync, Interval};
use fuchsia_framebuffer::ImageId;
use fuchsia_scenic::{EntityNode, SessionPtr, View};
use fuchsia_zircon::{Duration, Event, Time};
use futures::{channel::mpsc::UnboundedSender, StreamExt};

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
pub struct ViewAssistantContext {
    /// A unique key representing this view.
    pub key: ViewKey,
    /// The actual number of pixels in the view.
    pub size: Size,
    /// A factor representing pixel density. Use to
    /// calculate sizes for things like fonts.
    pub metrics: Size,
    /// For render, the time the rendering will be presented. Currently
    /// not implemented correctly.
    pub presentation_time: Time,
    /// When running in frame buffer mode, the number of buffers in
    /// the buffer collection
    pub buffer_count: Option<usize>,
    /// The ID of the Image being rendered in a buffer in
    /// preparation for being displayed. Used to keep track
    /// of what content needs to be rendered for a particular image
    /// in double or triple buffering configurations.
    pub image_id: ImageId,
    /// The index of the buffer in a buffer collection that is
    /// being used as the contents of the image specified in
    /// `image_id`.
    pub image_index: u32,

    messages: Vec<Message>,
}

impl ViewAssistantContext {
    /// Queue up a message for delivery
    pub fn queue_message(&mut self, message: Message) {
        self.messages.push(message);
    }
}

/// Trait that allows Carnelian developers to customize the behavior of views.
pub trait ViewAssistant {
    /// This method is called once when a view is created.
    #[allow(unused_variables)]
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called when a view needs to
    /// be rendered.
    #[allow(unused_variables)]
    fn render(
        &mut self,
        render_context: &mut Context,
        buffer_ready_event: Event,
        view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        anyhow::bail!("Assistant has ViewMode::Render but doesn't implement render.")
    }

    /// This method is called when input events come to this view. The default implementation
    /// calls specific methods for the type of event, so usually one does not need to implement
    /// this method. Since the default methods for touch and mouse handle the pointer abstraction,
    /// make sure to call them in an implementation of this method if you wish to use that
    /// abstraction.
    fn handle_input_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &input::Event,
    ) -> Result<(), Error> {
        match &event.event_type {
            input::EventType::Mouse(mouse_event) => {
                self.handle_mouse_event(context, event, mouse_event)
            }
            input::EventType::Touch(touch_event) => {
                self.handle_touch_event(context, event, touch_event)
            }
            input::EventType::Keyboard(keyboard_event) => {
                self.handle_keyboard_event(context, event, keyboard_event)
            }
        }
    }

    /// This method is called when mouse events come to this view.
    /// ```no_run
    /// # use anyhow::Error;
    /// # use carnelian::{
    /// #    input::{self},IntPoint,
    /// #    ViewAssistant, ViewAssistantContext,
    /// # };
    /// # struct SampleViewAssistant { mouse_start: IntPoint};
    /// impl ViewAssistant for SampleViewAssistant {
    ///     fn handle_mouse_event(
    ///         &mut self,
    ///         context: &mut ViewAssistantContext,
    ///         event: &input::Event,
    ///         mouse_event: &input::mouse::Event,
    ///     ) -> Result<(), Error> {
    ///         match &mouse_event.phase {
    ///             input::mouse::Phase::Down(button) => {
    ///                 if button.is_primary() {
    ///                     self.mouse_start = mouse_event.location
    ///                 }
    ///             }
    ///             input::mouse::Phase::Moved => {}
    ///             input::mouse::Phase::Up(button) => {
    ///                 if button.is_primary() {
    ///                     println!("mouse moved {}", mouse_event.location - self.mouse_start);
    ///                 }
    ///             }
    ///             _ => (),
    ///         }
    ///         Ok(())
    ///     }
    /// }
    /// ```
    fn handle_mouse_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &input::Event,
        mouse_event: &input::mouse::Event,
    ) -> Result<(), Error> {
        if self.uses_pointer_events() {
            if let Some(mouse_event) =
                input::pointer::Event::new_from_mouse_event(&event.device_id, mouse_event)
            {
                self.handle_pointer_event(context, event, &mouse_event)
            } else {
                Ok(())
            }
        } else {
            Ok(())
        }
    }

    /// This method is called when touch events come to this view.
    /// ```no_run
    /// # use anyhow::Error;
    /// # use carnelian::{
    /// #    input::{self},IntPoint,
    /// #    ViewAssistant, ViewAssistantContext,
    /// # };
    /// # struct SampleViewAssistant {};
    /// impl ViewAssistant for SampleViewAssistant {
    ///     fn handle_touch_event(
    ///         &mut self,
    ///         context: &mut ViewAssistantContext,
    ///         event: &input::Event,
    ///         touch_event: &input::touch::Event,
    ///     ) -> Result<(), Error> {
    ///         for contact in &touch_event.contacts {
    ///             // Use contact.contact_id as a key to handle
    ///             // each contact individually
    ///         }
    ///         Ok(())
    ///     }
    /// }
    /// ```
    fn handle_touch_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &input::Event,
        touch_event: &input::touch::Event,
    ) -> Result<(), Error> {
        if self.uses_pointer_events() {
            for contact in &touch_event.contacts {
                self.handle_pointer_event(
                    context,
                    event,
                    &input::pointer::Event::new_from_contact(contact),
                )?;
            }
            Ok(())
        } else {
            Ok(())
        }
    }

    /// This method is called when the view desires pointer events and a compatible
    /// mouse or touch event comes to this view.
    /// ```no_run
    /// # use anyhow::Error;
    /// # use carnelian::{
    /// #    input::{self},IntPoint,
    /// #    ViewAssistant, ViewAssistantContext,
    /// # };
    /// #    #[derive(Default)]
    /// #    struct SampleViewAssistant {
    /// #        mouse_start: IntPoint,
    /// #        pointer_start: IntPoint,
    /// #        current_pointer_location: IntPoint,
    /// #    }
    /// impl ViewAssistant for SampleViewAssistant {
    ///     fn handle_pointer_event(
    ///         &mut self,
    ///         context: &mut ViewAssistantContext,
    ///         event: &input::Event,
    ///         pointer_event: &input::pointer::Event,
    ///     ) -> Result<(), Error> {
    ///         match pointer_event.phase {
    ///             input::pointer::Phase::Down(pointer_location) => {
    ///                 self.pointer_start = pointer_location
    ///             }
    ///             input::pointer::Phase::Moved(pointer_location) => {
    ///                 self.current_pointer_location = pointer_location;
    ///             }
    ///             input::pointer::Phase::Up => {
    ///                 println!(
    ///                     "pointer moved {}",
    ///                     self.current_pointer_location - self.pointer_start
    ///                 );
    ///             }
    ///             _ => (),
    ///         }
    ///         Ok(())
    ///     }
    /// }
    /// ```
    #[allow(unused_variables)]
    fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &input::Event,
        pointer_event: &input::pointer::Event,
    ) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called when keyboard events come to this view.
    #[allow(unused_variables)]
    fn handle_keyboard_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called when focus events come from Scenic to this view. It will be
    /// called once when a Carnelian app is running directly on the frame buffer, as such
    /// views are always focused. See the button sample for an one way to respond to focus.
    #[allow(unused_variables)]
    fn handle_focus_event(&mut self, focused: bool) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called when `App::send_message` is called with the associated
    /// view controller's `ViewKey` and the view controller does not handle the message.
    /// ```no_run
    /// # use anyhow::Error;
    /// # use carnelian::{
    /// #     input::{self},
    /// #     IntPoint, Message, ViewAssistant, ViewAssistantContext,
    /// # };
    /// # #[derive(Default)]
    /// # struct SampleViewAssistant {}
    /// use fuchsia_zircon::Time;
    /// pub enum SampleMessages {
    ///     Pressed(Time),
    /// }
    /// impl ViewAssistant for SampleViewAssistant {
    ///     fn handle_message(&mut self, message: Message) {
    ///         if let Some(sample_message) = message.downcast_ref::<SampleMessages>() {
    ///             match sample_message {
    ///                 SampleMessages::Pressed(value) => {
    ///                     println!("value = {:#?}", value);
    ///                 }
    ///             }
    ///         }
    ///     }
    /// }
    /// ```
    #[allow(unused_variables)]
    fn handle_message(&mut self, message: Message) {}

    /// Initial animation mode for view. Default to [`AnimationMode::None`], which
    /// requires an view to be sent an [`ViewMessages::Update`] message when the
    /// view should be redrawn.
    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::None;
    }

    /// Whether this view wants touch and mouse events abstracted as
    /// [`input::pointer::Event`](./input/pointer/struct.Event.html). Defaults to true.
    fn uses_pointer_events(&self) -> bool {
        true
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
    presentation_interval: u64,
    next_presentation_time: u64,
    last_presentation_time: u64,
    app_sender: UnboundedSender<MessageInternal>,
}

impl ScenicResources {
    fn new(
        session: &SessionPtr,
        view_token: ViewToken,
        control_ref: ViewRefControl,
        view_ref: ViewRef,
        app_sender: UnboundedSender<MessageInternal>,
    ) -> ScenicResources {
        let view = View::new3(
            session.clone(),
            view_token,
            control_ref,
            view_ref,
            Some(String::from("Carnelian View")),
        );
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
            presentation_interval: 0,
            next_presentation_time: 0,
            last_presentation_time: 0,
            app_sender,
        }
    }
}

fn scenic_present(scenic_resources: &mut ScenicResources, key: ViewKey) {
    if scenic_resources.pending_present_count < 3 {
        let app_sender = scenic_resources.app_sender.clone();
        let presentation_time = scenic_resources.next_presentation_time;
        let present_event = scenic_resources.session.lock().present(presentation_time);
        fasync::spawn_local(async move {
            let info = present_event.await.expect("to present");
            app_sender
                .unbounded_send(MessageInternal::ScenicPresentDone(key, info))
                .expect("unbounded_send");
        });
        // Advance presentation time.
        scenic_resources.pending_present_count += 1;
        scenic_resources.last_presentation_time = presentation_time;
        scenic_resources.next_presentation_time += scenic_resources.presentation_interval;
    }
}

fn scenic_present_done(
    scenic_resources: &mut ScenicResources,
    info: fidl_fuchsia_images::PresentationInfo,
) {
    assert_ne!(scenic_resources.pending_present_count, 0);
    scenic_resources.pending_present_count -= 1;
    scenic_resources.presentation_interval = info.presentation_interval;
    // Determine next presentation time relative to presentation that
    // is no longer pending. Add one to ensure that presentation is past
    // previous.
    let next_presentation_time = info.presentation_time + 1;
    // Re-calculate next presentation time based on number of
    // presentations pending and make sure it is never before the
    // last presentation time.
    scenic_resources.next_presentation_time = scenic_resources.last_presentation_time.max(
        next_presentation_time
            + info.presentation_interval * scenic_resources.pending_present_count as u64,
    );
}

#[derive(Debug)]
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
        control_ref: ViewRefControl,
        view_ref: ViewRef,
        render_options: RenderOptions,
        session: SessionPtr,
        mut view_assistant: ViewAssistantPtr,
        app_sender: UnboundedSender<MessageInternal>,
    ) -> Result<ViewController, Error> {
        let strategy = ScenicViewStrategy::new(
            &session,
            render_options,
            view_token,
            control_ref,
            view_ref,
            app_sender.clone(),
        )
        .await?;

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
    pub(crate) async fn new_with_frame_buffer(
        key: ViewKey,
        render_options: RenderOptions,
        size: IntSize,
        pixel_format: fuchsia_framebuffer::PixelFormat,
        mut view_assistant: ViewAssistantPtr,
        app_sender: UnboundedSender<MessageInternal>,
        frame_buffer: FrameBufferPtr,
    ) -> Result<ViewController, Error> {
        let strategy = FrameBufferViewStrategy::new(
            key,
            render_options,
            &size,
            pixel_format,
            app_sender.clone(),
            frame_buffer,
        )
        .await?;
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

    pub(crate) fn present_done(&mut self, info: fidl_fuchsia_images::PresentationInfo) {
        self.strategy.present_done(&self.make_view_details(), &mut self.assistant, info);
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

    pub(crate) fn focus(&mut self, focus: bool) {
        self.assistant.handle_focus_event(focus).expect("handle_focus_event");
        self.update();
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
                    self.handle_metrics_changed(&event.metrics);
                }
                fidl_fuchsia_ui_gfx::Event::ViewPropertiesChanged(event) => {
                    self.handle_view_properties_changed(&event.properties);
                }
                _ => (),
            },
            fidl_fuchsia_ui_scenic::Event::Input(event) => match event {
                fidl_fuchsia_ui_input::InputEvent::Focus(focus_event) => {
                    self.focus(focus_event.focused);
                }
                _ => {
                    let messages = self.strategy.handle_scenic_input_event(
                        &self.make_view_details(),
                        &mut self.assistant,
                        &event,
                    );
                    for msg in messages {
                        self.send_message(msg);
                    }
                }
            },
            _ => (),
        });
    }

    /// Handler for Events on this ViewController's Session.
    pub fn handle_input_events(&mut self, events: Vec<input::Event>) {
        for event in events {
            let messages = self.strategy.handle_input_event(
                &self.make_view_details(),
                &mut self.assistant,
                &event,
            );
            for msg in messages {
                self.send_message(msg);
            }
        }
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

    pub fn handle_vsync_cookie(&mut self, cookie: u64) {
        self.strategy.handle_vsync_cookie(cookie);
    }
}
