// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{app::App, geometry::Size};
use failure::Error;
use fidl::endpoints::{create_endpoints, create_proxy, ServerEnd};
use fidl_fuchsia_ui_gfx as gfx;
use fidl_fuchsia_ui_input;
use fidl_fuchsia_ui_scenic::{SessionListenerMarker, SessionListenerRequest};
use fidl_fuchsia_ui_viewsv1::{ViewListenerMarker, ViewListenerRequest};
use fidl_fuchsia_ui_viewsv1token::ViewOwnerMarker;
use fuchsia_async as fasync;
use fuchsia_scenic::{ImportNode, Session, SessionPtr};
use fuchsia_zircon::{self as zx, EventPair};
use futures::{TryFutureExt, TryStreamExt};
use parking_lot::Mutex;
use std::{any::Any, cell::RefCell, sync::Arc};

/// enum that defines all messages sent with `App::send_message` that
/// the view struct will understand and process.
pub enum ViewMessages {
    /// Message that requests that a view redraw itself.
    Update,
}

/// parameter struct passed to setup and update trait methods.
#[allow(missing_docs)]
pub struct ViewAssistantContext<'a> {
    pub view_container: &'a mut fidl_fuchsia_ui_viewsv1::ViewContainerProxy,
    pub import_node: &'a ImportNode,
    pub session: &'a SessionPtr,
    pub key: ViewKey,
    pub size: Size,
    pub messages: Vec<Box<dyn Any>>,
}

impl<'a> ViewAssistantContext<'a> {
    /// Queue up a message for delivery
    pub fn queue_message<A: Any>(&mut self, message: A) {
        self.messages.push(Box::new(message));
    }
}

/// Trait that allows mod developers to customize the behavior of view controllers.
pub trait ViewAssistant: Send {
    /// This method is called once when a view is created. It is a good point to create scenic
    /// commands that apply throughout the lifetime of the view.
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error>;

    /// This method is called when a view controller has been asked to update the view.
    fn update(&mut self, context: &ViewAssistantContext) -> Result<(), Error>;

    /// This method is called when input events come from scenic to this view.
    fn handle_input_event(
        &mut self,
        _context: &mut ViewAssistantContext,
        _event: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called when `App::send_message` is called with the associated
    /// view controller's `ViewKey` and the view controller does not handle the message.
    fn handle_message(&mut self, message: &Any);
}

/// Reference to an app assistant. _This type is likely to change in the future so
/// using this type alias might make for easier forward migration._
pub type ViewAssistantPtr = Mutex<RefCell<Box<ViewAssistant>>>;

/// Key identifying a view.
pub type ViewKey = u64;

/// This struct takes care of all the boilerplate needed for implementing a Fuchsia
/// view, forwarding the interesting implementation points to a struct implementing
/// the `ViewAssistant` trait.
pub struct ViewController {
    #[allow(unused)]
    view: fidl_fuchsia_ui_viewsv1::ViewProxy,
    view_container: fidl_fuchsia_ui_viewsv1::ViewContainerProxy,
    session: SessionPtr,
    import_node: ImportNode,
    size: Size,
    #[allow(unused)]
    key: ViewKey,
    assistant: ViewAssistantPtr,
}

pub(crate) enum NewViewParams {
    V1(ServerEnd<ViewOwnerMarker>),
    V2(EventPair),
}

impl ViewController {
    pub(crate) fn new(
        app: &mut App,
        req: NewViewParams,
        key: ViewKey,
    ) -> Result<ViewControllerPtr, Error> {
        let (view, view_server_end) = create_proxy()?;
        let (view_listener, view_listener_request) = create_endpoints()?;
        let (mine, theirs) = zx::EventPair::create()?;
        match req {
            NewViewParams::V1(req) => {
                app.view_manager.create_view(view_server_end, req, view_listener, theirs, None)?;
            }
            NewViewParams::V2(view_token) => {
                app.view_manager.create_view2(
                    view_server_end,
                    view_token,
                    view_listener,
                    theirs,
                    None,
                )?;
            }
        }
        let (scenic, scenic_request) = create_proxy()?;
        app.view_manager.get_scenic(scenic_request)?;
        let (session_listener, session_listener_request) = create_endpoints()?;
        let (session_proxy, session_request) = create_proxy()?;
        scenic.create_session(session_request, Some(session_listener))?;
        let session = Session::new(session_proxy);

        let view_assistant = app.create_view_assistant(&session)?;

        let mut import_node = ImportNode::new(session.clone(), mine);

        let (mut view_container, view_container_request) = create_proxy()?;

        view.get_container(view_container_request)?;

        let context = ViewAssistantContext {
            view_container: &mut view_container,
            import_node: &mut import_node,
            session: &session,
            key,
            size: Size::zero(),
            messages: Vec::new(),
        };
        view_assistant.lock().borrow_mut().setup(&context)?;

        let view_controller = ViewController {
            view,
            view_container: view_container,
            session,
            import_node,
            size: Size::zero(),
            key,
            assistant: view_assistant,
        };

        let view_controller = Arc::new(Mutex::new(view_controller));

        Self::setup_session_listener(&view_controller, session_listener_request)?;
        Self::setup_view_listener(&view_controller, view_listener_request)?;

        Ok(view_controller)
    }

    fn setup_session_listener(
        view_controller: &ViewControllerPtr,
        session_listener_request: ServerEnd<SessionListenerMarker>,
    ) -> Result<(), Error> {
        let view_controller = view_controller.clone();
        fasync::spawn(
            session_listener_request
                .into_stream()?
                .map_ok(move |request| match request {
                    SessionListenerRequest::OnScenicEvent { events, .. } => {
                        view_controller.lock().handle_session_events(events)
                    }
                    _ => (),
                })
                .try_collect::<()>()
                .unwrap_or_else(|e| eprintln!("view listener error: {:?}", e)),
        );

        Ok(())
    }

    fn setup_view_listener(
        view_controller: &ViewControllerPtr,
        view_listener_request: ServerEnd<ViewListenerMarker>,
    ) -> Result<(), Error> {
        let view_controller = view_controller.clone();
        fasync::spawn(
            view_listener_request
                .into_stream()?
                .try_for_each(
                    move |ViewListenerRequest::OnPropertiesChanged { properties, responder }| {
                        view_controller.lock().handle_properties_changed(&properties);
                        futures::future::ready(responder.send())
                    },
                )
                .unwrap_or_else(|e| eprintln!("view listener error: {:?}", e)),
        );

        Ok(())
    }

    fn update(&mut self) {
        let context = ViewAssistantContext {
            view_container: &mut self.view_container,
            import_node: &mut self.import_node,
            session: &self.session,
            key: self.key,
            size: self.size,
            messages: Vec::new(),
        };
        self.assistant
            .lock()
            .borrow_mut()
            .update(&context)
            .unwrap_or_else(|e| eprintln!("Update error: {:?}", e));
        self.present();
    }

    fn handle_session_events(&mut self, events: Vec<fidl_fuchsia_ui_scenic::Event>) {
        events.iter().for_each(|event| match event {
            fidl_fuchsia_ui_scenic::Event::Gfx(gfx::Event::Metrics(_event)) => {
                self.update();
            }
            fidl_fuchsia_ui_scenic::Event::Input(event) => {
                let mut context = ViewAssistantContext {
                    view_container: &mut self.view_container,
                    import_node: &mut self.import_node,
                    session: &self.session,
                    key: self.key,
                    size: self.size,
                    messages: Vec::new(),
                };
                self.assistant
                    .lock()
                    .borrow_mut()
                    .handle_input_event(&mut context, &event)
                    .unwrap_or_else(|e| eprintln!("handle_event: {:?}", e));
                for msg in context.messages {
                    self.send_message(&msg);
                }
                self.update();
            }
            _ => (),
        });
    }

    fn present(&self) {
        fasync::spawn(
            self.session
                .lock()
                .present(0)
                .map_ok(|_| ())
                .unwrap_or_else(|e| eprintln!("present error: {:?}", e)),
        );
    }

    fn handle_properties_changed(&mut self, properties: &fidl_fuchsia_ui_viewsv1::ViewProperties) {
        if let Some(ref view_properties) = properties.view_layout {
            self.size = Size::new(view_properties.size.width, view_properties.size.height);
            self.update();
        }
    }

    /// This method sends an arbitrary message to this view. If it is not
    /// handled directly by `ViewController::send_message` it will be forwarded
    /// to the view assistant.
    pub fn send_message(&mut self, msg: &Any) {
        if let Some(view_msg) = msg.downcast_ref::<ViewMessages>() {
            match view_msg {
                ViewMessages::Update => {
                    self.update();
                }
            }
        } else {
            self.assistant.lock().borrow_mut().handle_message(msg);
        }
    }
}

pub type ViewControllerPtr = Arc<Mutex<ViewController>>;
