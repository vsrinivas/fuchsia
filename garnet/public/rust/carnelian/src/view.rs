// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{geometry::Size, message::Message};
use failure::Error;
use fidl_fuchsia_ui_gfx::{self as gfx, Metrics, ViewProperties};
use fidl_fuchsia_ui_input::InputEvent;
use fidl_fuchsia_ui_views::ViewToken;
use fuchsia_async as fasync;
use fuchsia_scenic::{EntityNode, SessionPtr, View};
use futures::TryFutureExt;
use std::any::Any;

/// enum that defines all messages sent with `App::send_message` that
/// the view struct will understand and process.
pub enum ViewMessages {
    /// Message that requests that a view redraw itself.
    Update,
}

/// parameter struct passed to setup and update trait methods.
#[allow(missing_docs)]
pub struct ViewAssistantContext<'a> {
    pub view: &'a View,
    pub root_node: &'a EntityNode,
    pub session: &'a SessionPtr,
    pub logical_size: Size,
    pub size: Size,
    pub metrics: Size,
    pub messages: Vec<Box<&'static Any>>,
}

impl<'a> ViewAssistantContext<'a> {
    /// Queue up a message for delivery
    pub fn queue_message(&mut self, message: Message) {
        self.messages.push(message);
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
        _context: &mut ViewAssistantContext,
        _event: &InputEvent,
    ) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called when `App::send_message` is called with the associated
    /// view controller's `ViewKey` and the view controller does not handle the message.
    fn handle_message(&mut self, _message: Message) {}
}

/// Reference to a view assistant. _This type is likely to change in the future so
/// using this type alias might make for easier forward migration._
pub type ViewAssistantPtr = Box<dyn ViewAssistant>;

/// Key identifying a view.
pub type ViewKey = u64;

/// This struct takes care of all the boilerplate needed for implementing a Fuchsia
/// view, forwarding the interesting implementation points to a struct implementing
/// the `ViewAssistant` trait.
pub struct ViewController {
    view: View,
    root_node: EntityNode,
    session: SessionPtr,
    #[allow(unused)]
    assistant: ViewAssistantPtr,
    metrics: Size,
    physical_size: Size,
    logical_size: Size,
}

impl ViewController {
    pub(crate) fn new(
        view_token: ViewToken,
        session: SessionPtr,
        mut view_assistant: ViewAssistantPtr,
    ) -> Result<ViewController, Error> {
        let view = View::new(session.clone(), view_token, Some(String::from("Carnelian View")));
        let root_node = EntityNode::new(session.clone());
        root_node.resource().set_event_mask(gfx::METRICS_EVENT_MASK);
        view.add_child(&root_node);

        let context = ViewAssistantContext {
            view: &view,
            root_node: &root_node,
            session: &session,
            logical_size: Size::zero(),
            size: Size::zero(),
            metrics: Size::zero(),
            messages: Vec::new(),
        };
        view_assistant.setup(&context)?;
        let view_controller = ViewController {
            view,
            root_node,
            session,
            metrics: Size::zero(),
            physical_size: Size::zero(),
            logical_size: Size::zero(),
            assistant: view_assistant,
        };

        Ok(view_controller)
    }

    /// Handler for Events on this ViewController's |Session|.
    pub fn handle_session_events(&mut self, events: Vec<fidl_fuchsia_ui_scenic::Event>) {
        events.iter().for_each(|event| match event {
            fidl_fuchsia_ui_scenic::Event::Gfx(event) => match event {
                fidl_fuchsia_ui_gfx::Event::Metrics(event) => {
                    assert!(event.node_id == self.root_node.id());
                    self.handle_metrics_changed(&event.metrics);
                }
                fidl_fuchsia_ui_gfx::Event::ViewPropertiesChanged(event) => {
                    assert!(event.view_id == self.view.id());
                    self.handle_view_properties_changed(&event.properties);
                }
                _ => (),
            },
            fidl_fuchsia_ui_scenic::Event::Input(event) => {
                let mut context = ViewAssistantContext {
                    view: &self.view,
                    root_node: &self.root_node,
                    session: &self.session,
                    logical_size: self.logical_size,
                    size: self.physical_size,
                    metrics: self.metrics,
                    messages: Vec::new(),
                };
                self.assistant
                    .handle_input_event(&mut context, &event)
                    .unwrap_or_else(|e| eprintln!("handle_event: {:?}", e));
                for msg in context.messages {
                    self.send_message(msg);
                }
                self.update();
            }
            _ => (),
        });
    }

    /// Informs Scenic of any changes made to this View's |Session|.
    pub fn present(&self) {
        fasync::spawn_local(
            self.session
                .lock()
                .present(0)
                .map_ok(|_| ())
                .unwrap_or_else(|e| panic!("present error: {:?}", e)),
        );
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

    fn update(&mut self) {
        // Recompute our logical size based on the provided physical size and screen metrics.
        self.logical_size = Size::new(
            self.physical_size.width * self.metrics.width,
            self.physical_size.height * self.metrics.height,
        );

        let context = ViewAssistantContext {
            view: &self.view,
            root_node: &self.root_node,
            session: &self.session,
            logical_size: self.logical_size,
            size: self.physical_size,
            metrics: self.metrics,
            messages: Vec::new(),
        };
        self.assistant.update(&context).unwrap_or_else(|e| panic!("Update error: {:?}", e));
        self.present();
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
}
