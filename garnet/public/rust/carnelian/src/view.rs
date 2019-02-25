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
use fuchsia_async as fasync;
use fuchsia_scenic::{ImportNode, Session, SessionPtr};
use fuchsia_zircon as zx;
use futures::{TryFutureExt, TryStreamExt};
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
    pub view_container: &'a mut fidl_fuchsia_ui_viewsv1::ViewContainerProxy,
    pub import_node: &'a ImportNode,
    pub session: &'a SessionPtr,
    pub key: ViewKey,
    pub logical_size: Size,
    pub size: Size,
    pub metrics: Size,
    pub messages: Vec<Box<dyn Any>>,
}

impl<'a> ViewAssistantContext<'a> {
    /// Queue up a message for delivery
    pub fn queue_message<A: Any>(&mut self, message: A) {
        self.messages.push(Box::new(message));
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
        _event: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Result<(), Error> {
        Ok(())
    }

    /// This method is called when `App::send_message` is called with the associated
    /// view controller's `ViewKey` and the view controller does not handle the message.
    fn handle_message(&mut self, _message: &Any) {}
}

/// Reference to an app assistant. _This type is likely to change in the future so
/// using this type alias might make for easier forward migration._
pub type ViewAssistantPtr = Box<dyn ViewAssistant>;

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
    #[allow(unused)]
    key: ViewKey,
    assistant: ViewAssistantPtr,
    metrics: Size,
    physical_size: Size,
    logical_size: Size,
}

impl ViewController {
    pub(crate) fn new(
        app: &mut App,
        view_token: gfx::ExportToken,
        key: ViewKey,
    ) -> Result<ViewController, Error> {
        let (view, view_server_end) = create_proxy()?;
        let (view_listener, view_listener_request) = create_endpoints()?;
        let (mine, theirs) = zx::EventPair::create()?;

        app.view_manager.create_view2(
            view_server_end,
            view_token.value,
            view_listener,
            theirs,
            None,
        )?;

        let (session_listener, session_listener_request) = create_endpoints()?;
        let (session_proxy, session_request) = create_proxy()?;
        app.scenic.create_session(session_request, Some(session_listener))?;
        let session = Session::new(session_proxy);

        let mut view_assistant = app.create_view_assistant(&session)?;

        let mut import_node = ImportNode::new(session.clone(), mine);

        let (mut view_container, view_container_request) = create_proxy()?;

        view.get_container(view_container_request)?;

        let context = ViewAssistantContext {
            view_container: &mut view_container,
            import_node: &mut import_node,
            session: &session,
            key,
            logical_size: Size::zero(),
            size: Size::zero(),
            metrics: Size::zero(),
            messages: Vec::new(),
        };
        view_assistant.setup(&context)?;

        let view_controller = ViewController {
            view,
            view_container: view_container,
            session,
            import_node,
            metrics: Size::zero(),
            physical_size: Size::zero(),
            logical_size: Size::zero(),
            key,
            assistant: view_assistant,
        };

        Self::setup_session_listener(key, session_listener_request)?;
        Self::setup_view_listener(key, view_listener_request)?;

        Ok(view_controller)
    }

    fn setup_session_listener(
        key: ViewKey,
        session_listener_request: ServerEnd<SessionListenerMarker>,
    ) -> Result<(), Error> {
        fasync::spawn_local(
            session_listener_request
                .into_stream()?
                .map_ok(move |request| match request {
                    SessionListenerRequest::OnScenicEvent { events, .. } => App::with(|app| {
                        app.with_view(key, |view| {
                            view.handle_session_events(events);
                        })
                    }),
                    _ => (),
                })
                .try_collect::<()>()
                .unwrap_or_else(|e| eprintln!("view listener error: {:?}", e)),
        );

        Ok(())
    }

    fn setup_view_listener(
        key: ViewKey,
        view_listener_request: ServerEnd<ViewListenerMarker>,
    ) -> Result<(), Error> {
        fasync::spawn_local(
            view_listener_request
                .into_stream()?
                .try_for_each(
                    move |ViewListenerRequest::OnPropertiesChanged { properties, responder }| {
                        App::with(|app| {
                            app.with_view(key, |view| {
                                view.handle_properties_changed(&properties);
                            });
                        });
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
            logical_size: self.logical_size,
            size: self.physical_size,
            metrics: self.metrics,
            messages: Vec::new(),
        };
        self.assistant.update(&context).unwrap_or_else(|e| panic!("Update error: {:?}", e));
        self.present();
    }

    fn handle_session_events(&mut self, events: Vec<fidl_fuchsia_ui_scenic::Event>) {
        events.iter().for_each(|event| match event {
            fidl_fuchsia_ui_scenic::Event::Gfx(gfx::Event::Metrics(event)) => {
                self.metrics = Size::new(event.metrics.scale_x, event.metrics.scale_y);
                self.logical_size = Size::new(
                    self.physical_size.width * self.metrics.width,
                    self.physical_size.height * self.metrics.height,
                );
                self.update();
            }
            fidl_fuchsia_ui_scenic::Event::Input(event) => {
                let mut context = ViewAssistantContext {
                    view_container: &mut self.view_container,
                    import_node: &mut self.import_node,
                    session: &self.session,
                    key: self.key,
                    logical_size: self.logical_size,
                    size: self.physical_size,
                    metrics: self.metrics,
                    messages: Vec::new(),
                };
                self.assistant
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
        fasync::spawn_local(
            self.session
                .lock()
                .present(0)
                .map_ok(|_| ())
                .unwrap_or_else(|e| panic!("present error: {:?}", e)),
        );
    }

    fn handle_properties_changed(&mut self, properties: &fidl_fuchsia_ui_viewsv1::ViewProperties) {
        if let Some(ref view_properties) = properties.view_layout {
            self.physical_size = Size::new(view_properties.size.width, view_properties.size.height);
            self.logical_size = Size::new(
                self.physical_size.width * self.metrics.width,
                self.physical_size.height * self.metrics.height,
            );
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
            self.assistant.handle_message(msg);
        }
    }
}
