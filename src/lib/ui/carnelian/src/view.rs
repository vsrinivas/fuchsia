// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::MessageInternal,
    geometry::{IntPoint, Size},
    input::{self, UserInputMessage},
    message::Message,
    render::Context,
    scene::{facets::FacetId, scene::Scene},
    view::strategies::base::ViewStrategyPtr,
    IdFromRaw, MessageTarget,
};
use anyhow::{ensure, Error};
use euclid::size2;
use fuchsia_framebuffer::ImageId;
use fuchsia_scenic::View;
use fuchsia_trace::instant;
use fuchsia_zircon::{Event, Time};
use futures::channel::mpsc::{unbounded, UnboundedSender};
use std::fmt::{Display, Formatter};

pub(crate) mod strategies;

#[derive(Debug, Clone)]
pub struct DisplayInfo {
    pub id: u64,
    pub horizontal_size_mm: u32,
    pub vertical_size_mm: u32,
    pub using_fallback_size: bool,
}

impl From<&fidl_fuchsia_hardware_display::Info> for DisplayInfo {
    fn from(info: &fidl_fuchsia_hardware_display::Info) -> Self {
        Self {
            id: info.id,
            horizontal_size_mm: info.horizontal_size_mm,
            vertical_size_mm: info.vertical_size_mm,
            using_fallback_size: info.using_fallback_size,
        }
    }
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
    /// Position of the mouse cursor when running directly on the
    /// display controller.
    pub mouse_cursor_position: Option<IntPoint>,
    /// information about the hosting display when running directly on the
    /// display controller.
    pub display_info: Option<DisplayInfo>,

    app_sender: UnboundedSender<MessageInternal>,
}

impl ViewAssistantContext {
    /// Returns an empty ViewAssistantContext to enable testing with mocks
    pub fn new_for_testing() -> Self {
        let (unbounded_sender, _) = unbounded::<MessageInternal>();
        Self {
            key: Default::default(),
            size: Default::default(),
            metrics: Default::default(),
            presentation_time: Default::default(),
            buffer_count: Default::default(),
            image_id: Default::default(),
            image_index: Default::default(),
            mouse_cursor_position: Default::default(),
            display_info: Default::default(),
            app_sender: unbounded_sender,
        }
    }

    /// Queue up a message for delivery
    pub fn queue_message(&mut self, message: Message) {
        self.app_sender
            .unbounded_send(MessageInternal::TargetedMessage(
                MessageTarget::View(self.key),
                message,
            ))
            .expect("ViewAssistantContext::queue_message - unbounded_send");
    }

    /// Request that a render occur for this view at the next
    /// appropriate time to render.
    pub fn request_render(&self) {
        self.app_sender
            .unbounded_send(MessageInternal::RequestRender(self.key))
            .expect("unbounded_send");
    }
}

/// Trait that allows Carnelian developers to customize the behavior of views.
pub trait ViewAssistant {
    /// This method is called once when a view is created.
    #[allow(unused_variables)]
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        Ok(())
    }

    #[allow(unused_variables)]
    /// Implement this method to to handle when a view is resized.
    fn resize(&mut self, new_size: &Size) -> Result<(), Error> {
        Ok(())
    }

    #[allow(unused_variables)]
    /// Implement this method to return a mutable reference to the scene that
    /// represents the view.
    fn get_scene(&mut self, size: Size) -> Option<&mut Scene> {
        None
    }

    #[allow(unused_variables)]
    /// Implement this method to return a mutable reference to the scene that
    /// represents the view. Implement this one if you'll need the various
    /// contexts to build a scene.
    fn get_scene_with_contexts(
        &mut self,
        render_context: &mut Context,
        view_context: &ViewAssistantContext,
    ) -> Option<&mut Scene> {
        self.get_scene(view_context.size)
    }

    /// This method is called when a view needs to
    /// be rendered.
    fn render(
        &mut self,
        render_context: &mut Context,
        buffer_ready_event: Event,
        view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        if let Some(scene) = self.get_scene_with_contexts(render_context, view_context) {
            scene.layout(view_context.size);
            scene.render(render_context, buffer_ready_event, view_context)?;
            if scene.is_animated() {
                view_context.request_render();
            }
            Ok(())
        } else {
            anyhow::bail!("Assistant has ViewMode::Render but doesn't implement render or scene.")
        }
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
            input::EventType::ConsumerControl(consumer_control_event) => {
                self.handle_consumer_control_event(context, event, consumer_control_event)
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

    /// This method is called when consumer control events come to this view.
    #[allow(unused_variables)]
    fn handle_consumer_control_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &input::Event,
        consumer_control_event: &input::consumer_control::Event,
    ) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called when focus events come from Scenic to this view. It will be
    /// called once when a Carnelian app is running directly on the frame buffer, as such
    /// views are always focused. See the button sample for an one way to respond to focus.
    #[allow(unused_variables)]
    fn handle_focus_event(
        &mut self,
        context: &mut ViewAssistantContext,
        focused: bool,
    ) -> Result<(), Error> {
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

    /// Whether this view wants touch and mouse events abstracted as
    /// [`input::pointer::Event`](./input/pointer/struct.Event.html). Defaults to true.
    fn uses_pointer_events(&self) -> bool {
        true
    }

    /// This method is called when running directly on the display and the ownership
    /// of the display changes.
    fn ownership_changed(&mut self, _owned: bool) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called after setup to get an offset to use when calculating
    /// render time. It is only called once.
    fn get_render_offset(&mut self) -> Option<i64> {
        None
    }
}

/// Reference to a view assistant. _This type is likely to change in the future so
/// using this type alias might make for easier forward migration._
pub type ViewAssistantPtr = Box<dyn ViewAssistant>;

/// Key identifying a view.
#[derive(Debug, Default, Clone, Copy, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct ViewKey(pub u64);

impl IdFromRaw for ViewKey {
    fn from_raw(id: u64) -> ViewKey {
        ViewKey(id)
    }
}

impl Display for ViewKey {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), std::fmt::Error> {
        write!(f, "ViewKey({})", self.0)
    }
}

#[derive(Debug)]
pub(crate) struct ViewDetails {
    key: ViewKey,
    metrics: Size,
    physical_size: Size,
    logical_size: Size,
}

/// This struct takes care of all the boilerplate needed for implementing a Fuchsia
/// view, forwarding the interesting implementation points to a struct implementing
/// the `ViewAssistant` trait.
pub(crate) struct ViewController {
    key: ViewKey,
    assistant: ViewAssistantPtr,
    metrics: Size,
    physical_size: Size,
    logical_size: Size,
    render_requested: bool,
    strategy: ViewStrategyPtr,
    app_sender: UnboundedSender<MessageInternal>,
}

impl ViewController {
    pub async fn new_with_strategy(
        key: ViewKey,
        view_assistant: ViewAssistantPtr,
        strategy: ViewStrategyPtr,
        app_sender: UnboundedSender<MessageInternal>,
    ) -> Result<ViewController, Error> {
        let metrics = strategy.initial_metrics();
        let physical_size = strategy.initial_physical_size();
        let logical_size = strategy.initial_logical_size();

        let mut view_controller = ViewController {
            key,
            metrics,
            physical_size,
            logical_size,
            render_requested: true,
            assistant: view_assistant,
            strategy,
            app_sender,
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
        }
    }

    /// Informs Scenic of any changes made to this View's |Session|.
    pub fn present(&mut self) {
        self.strategy.present(&self.make_view_details());
    }

    pub fn send_update_message(&mut self) {
        self.app_sender.unbounded_send(MessageInternal::Render(self.key)).expect("unbounded_send");
    }

    pub fn ownership_changed(&mut self, owned: bool) {
        self.strategy.ownership_changed(owned);
        self.assistant
            .ownership_changed(owned)
            .unwrap_or_else(|e| println!("ownership_changed error: {}", e));
    }

    pub fn drop_display_resources(&mut self) {
        self.strategy.drop_display_resources();
    }

    pub fn request_render(&mut self) {
        self.render_requested = true;
        self.strategy.render_requested();
    }

    pub async fn render(&mut self) {
        if self.render_requested {
            // Recompute our physical size based on the provided logical size and screen metrics.
            self.physical_size = size2(
                self.logical_size.width * self.metrics.width,
                self.logical_size.height * self.metrics.height,
            );

            if self.strategy.render(&self.make_view_details(), &mut self.assistant).await {
                self.render_requested = false;
            }
            self.present();
        }
    }

    pub fn present_submitted(
        &mut self,
        info: fidl_fuchsia_scenic_scheduling::FuturePresentationTimes,
    ) {
        self.strategy.present_submitted(&self.make_view_details(), &mut self.assistant, info);
    }

    pub fn present_done(&mut self, info: fidl_fuchsia_scenic_scheduling::FramePresentedInfo) {
        self.strategy.present_done(&self.make_view_details(), &mut self.assistant, info);
    }

    pub fn handle_metrics_changed(&mut self, metrics: Size) {
        if self.metrics != metrics {
            instant!(
                "gfx",
                "ViewController::metrics_changed",
                fuchsia_trace::Scope::Process,
                "old_device_pixel_ratio_x" => self.metrics.width as f64,
                "old_device_pixel_ratio_y" => self.metrics.height as f64,
                "new_device_pixel_ratio_x" => metrics.width as f64,
                "new_device_pixel_ratio_y" => metrics.height as f64
            );
            self.metrics = metrics;
            self.render_requested = true;
            self.send_update_message();
        }
    }

    pub fn handle_size_changed(&mut self, new_size: Size) {
        if self.logical_size != new_size {
            instant!(
                "gfx",
                "ViewController::size_changed",
                fuchsia_trace::Scope::Process,
                "old_logical_width" => self.logical_size.width as f64,
                "old_logical_height" => self.logical_size.height as f64,
                "new_logical_width" => new_size.width as f64,
                "new_logical_height" => new_size.height as f64
            );
            self.logical_size = new_size;
            self.assistant
                .resize(&new_size)
                .unwrap_or_else(|e| println!("handle_size_changed error: {}", e));
            self.render_requested = true;
            self.send_update_message();
        }
    }

    pub fn focus(&mut self, focus: bool) {
        self.strategy.handle_focus(&self.make_view_details(), &mut self.assistant, focus);
    }

    fn handle_input_events_internal(
        &mut self,
        view_details: &ViewDetails,
        events: Vec<input::Event>,
    ) -> Result<(), Error> {
        let mut view_assistant_context = self.strategy.create_view_assistant_context(&view_details);
        for event in events {
            self.strategy.inspect_event(view_details, &event);
            self.assistant.handle_input_event(&mut view_assistant_context, &event)?;
        }
        Ok(())
    }

    pub fn handle_user_input_message(
        &mut self,
        user_input_message: UserInputMessage,
    ) -> Result<(), Error> {
        let view_details = self.make_view_details();
        let events = self.strategy.convert_user_input_message(&view_details, user_input_message)?;
        self.handle_input_events_internal(&view_details, events)?;
        Ok(())
    }

    /// Handle input events that have been converted to Carnelian's format
    pub fn handle_input_events(&mut self, events: Vec<input::Event>) -> Result<(), Error> {
        let view_details = self.make_view_details();
        self.handle_input_events_internal(&view_details, events)?;
        Ok(())
    }

    /// This method sends an arbitrary message to this view to be
    /// forwarded to the view assistant.
    pub fn send_message(&mut self, msg: Message) {
        self.assistant.handle_message(msg);
    }

    /// This method sends an arbitrary message to this view to be
    /// forwarded to the view assistant.
    pub fn send_facet_message(&mut self, facet_id: FacetId, msg: Message) -> Result<(), Error> {
        let scene = self.assistant.get_scene(self.physical_size);
        ensure!(scene.is_some(), "send_facet_message called on view not providing a scene");
        let scene = scene.unwrap();
        scene.send_message(&facet_id, msg);
        Ok(())
    }

    pub fn image_freed(&mut self, image_id: u64, collection_id: u32) {
        self.strategy.image_freed(image_id, collection_id);
    }

    pub fn handle_on_next_frame_begin(
        &mut self,
        info: &fidl_fuchsia_ui_composition::OnNextFrameBeginValues,
    ) {
        self.strategy.handle_on_next_frame_begin(info);
    }

    pub async fn handle_display_controller_event(
        &mut self,
        event: fidl_fuchsia_hardware_display::ControllerEvent,
    ) {
        self.strategy.handle_display_controller_event(event).await;
    }

    pub fn is_hosted_on_display(&self, display_id: u64) -> bool {
        self.strategy.is_hosted_on_display(display_id)
    }

    pub fn close(&mut self) {
        self.strategy.close();
    }
}
