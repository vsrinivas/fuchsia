// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    message::Message,
    view::{ViewAssistantPtr, ViewController, ViewKey},
};
use failure::{bail, Error, ResultExt};
use fidl::endpoints::{create_endpoints, create_proxy};
use fidl_fuchsia_ui_app::{ViewProviderRequest, ViewProviderRequestStream};
use fidl_fuchsia_ui_scenic::{ScenicMarker, ScenicProxy, SessionListenerRequest};
use fidl_fuchsia_ui_views::ViewToken;
use fuchsia_async as fasync;
use fuchsia_component::{self as component, client::connect_to_service};
use fuchsia_scenic::{Session, SessionPtr};
use fuchsia_zircon as zx;
use futures::{Future, StreamExt, TryFutureExt, TryStreamExt};
use std::{
    cell::RefCell,
    collections::BTreeMap,
    mem,
    sync::atomic::{AtomicBool, Ordering},
};

/// Trait that a mod author must implement. Currently responsible for creating
/// a view assistant when the Fuchsia view framework requests that the mod create
/// a view.
pub trait AppAssistant {
    /// This method is responsible for setting up the AppAssistant implementation.
    /// _It's not clear if this is going to so useful, as anything that isn't
    /// initialized in the creation of the structure implementing AppAssistant
    /// is going to have to be represented as an `Option`, which is awkward._
    fn setup(&mut self) -> Result<(), Error>;

    /// Called when the Fuchsia view system requests that a view be created.
    fn create_view_assistant(
        &mut self,
        key: ViewKey,
        session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error>;

    /// Return the list of names of services this app wants to provide
    fn outgoing_services_names(&self) -> Vec<&'static str> {
        Vec::new()
    }

    /// Handle a request to connect to a service provided by this app
    fn handle_service_connection_request(
        &mut self,
        _service_name: &str,
        _channel: fasync::Channel,
    ) -> Result<(), Error> {
        bail!("handle_service_connection_request not implemented")
    }
}

pub type AppAssistantPtr = Box<dyn AppAssistant>;

/// Struct that implements module-wide responsibilities, currently limited
/// to creating views on request.
pub struct App {
    pub(crate) scenic: ScenicProxy,
    view_controllers: BTreeMap<ViewKey, ViewController>,
    next_key: ViewKey,
    assistant: Option<AppAssistantPtr>,
    messages: Vec<(ViewKey, Message)>,
}

/// Reference to the singleton app. _This type is likely to change in the future so
/// using this type alias might make for easier forward migration._
type AppPtr = RefCell<App>;

static DID_APP_INIT: AtomicBool = AtomicBool::new(false);

thread_local! {
    /// Singleton reference to the running application
    static APP: AppPtr = {
        if DID_APP_INIT.fetch_or(true, Ordering::SeqCst) {
            panic!("App::with() may only be called on the first thread that calls App::run()");
        }
        App::new().expect("Failed to create app")
    };
}

impl App {
    fn new() -> Result<AppPtr, Error> {
        let scenic = connect_to_service::<ScenicMarker>()?;
        Ok(RefCell::new(App {
            scenic,
            view_controllers: BTreeMap::new(),
            next_key: 0,
            assistant: None,
            messages: Vec::new(),
        }))
    }

    /// Starts an application based on Carnelian. The `assistant` parameter will
    /// be used to create new views when asked to do so by the Fuchsia view system.
    pub fn run(assistant: Box<AppAssistant>) -> Result<(), Error> {
        let mut executor = fasync::Executor::new().context("Error creating executor")?;

        let fut = App::with(|app| {
            app.set_assistant(assistant);
            let fut = App::start_services(app);
            app.assistant.as_mut().unwrap().setup()?;
            fut
        })?;

        executor.run_singlethreaded(fut);

        Ok(())
    }

    /// Function to get a mutable reference to the singleton app struct, useful
    /// in callbacks.
    pub fn with<F, R>(f: F) -> R
    where
        F: FnOnce(&mut App) -> R,
    {
        APP.with(|app| {
            let mut app_ref = app
                .try_borrow_mut()
                .expect("Attempted to call App::with() while already in a call to App::with()");
            let r = f(&mut app_ref);

            // Replace app's messages with an empty list before
            // sending them. This isn't strictly needed now as
            // queueing messages during `with` is not possible
            // but it will be in the future.
            let mut messages = Vec::new();
            mem::swap(&mut messages, &mut app_ref.messages);
            for (key, msg) in messages {
                app_ref.send_message(key, msg);
            }
            r
        })
    }

    /// Function to get a mutable reference to a view controller for a particular
    /// view, by view key. It is a fatal error to pass a view id that doesn't
    /// have a corresponding view controller.
    pub fn with_view<F>(&mut self, view_key: ViewKey, f: F)
    where
        F: FnOnce(&mut ViewController),
    {
        if let Some(view) = self.view_controllers.get_mut(&view_key) {
            f(view)
        } else {
            panic!("Could not find view controller for {}", view_key);
        }
    }

    /// Send a message to a specific view controller. Messages not handled by the ViewController
    /// will be forwarded to the `ViewControllerAssistant`.
    fn send_message(&mut self, target: ViewKey, msg: Message) {
        if let Some(view) = self.view_controllers.get_mut(&target) {
            view.send_message(msg);
        }
    }

    fn set_assistant(&mut self, assistant: AppAssistantPtr) {
        self.assistant = Some(assistant);
    }

    /// Send a message to a specific view controller. Messages not handled by the ViewController
    /// will be forwarded to the `ViewControllerAssistant`.
    pub fn queue_message(&mut self, target: ViewKey, msg: Message) {
        self.messages.push((target, msg));
    }

    fn setup_session(&self) -> Result<SessionPtr, Error> {
        let (session_listener, session_listener_request) = create_endpoints()?;
        let (session_proxy, session_request) = create_proxy()?;
        let view_id = self.next_key;

        self.scenic.create_session(session_request, Some(session_listener))?;
        fasync::spawn_local(
            session_listener_request
                .into_stream()?
                .map_ok(move |request| match request {
                    SessionListenerRequest::OnScenicEvent { events, .. } => App::with(|app| {
                        app.with_view(view_id, |view| {
                            view.handle_session_events(events);
                        })
                    }),
                    _ => (),
                })
                .try_collect::<()>()
                .unwrap_or_else(|e| eprintln!("view listener error: {:?}", e)),
        );

        Ok(Session::new(session_proxy))
    }

    fn create_view_assistant(&mut self, session: &SessionPtr) -> Result<ViewAssistantPtr, Error> {
        Ok(self.assistant.as_mut().unwrap().create_view_assistant(self.next_key, session)?)
    }

    fn create_view(&mut self, view_token: ViewToken) -> Result<(), Error> {
        let session = self.setup_session()?;
        let view_assistant = self.create_view_assistant(&session)?;
        let mut view_controller =
            ViewController::new(self.next_key, view_token, session, view_assistant)?;

        view_controller.setup_animation_mode();

        view_controller.present();
        self.view_controllers.insert(self.next_key, view_controller);
        self.next_key += 1;
        Ok(())
    }

    fn spawn_view_provider_server(stream: ViewProviderRequestStream) {
        fasync::spawn_local(
            stream
                .try_for_each(move |req| {
                    let ViewProviderRequest::CreateView { token, .. } = req;
                    let view_token = ViewToken { value: token };
                    App::with(|app| {
                        app.create_view(view_token)
                            .unwrap_or_else(|e| eprintln!("create_view error: {:?}", e));
                    });
                    futures::future::ready(Ok(()))
                })
                .unwrap_or_else(|e| eprintln!("error running ViewProvider server: {:?}", e)),
        )
    }

    fn pass_connection_to_assistant(channel: zx::Channel, service_name: &'static str) {
        match fasync::Channel::from_channel(channel) {
            Ok(channel) => {
                App::with(|app| {
                    app.assistant
                        .as_mut()
                        .unwrap()
                        .handle_service_connection_request(service_name, channel)
                        .unwrap_or_else(|e| eprintln!("error running {} server: {:?}", service_name, e));
                });
            },
            Err(e) => eprintln!("error asyncifying channel: {:?}", e),
        }
    }

    fn start_services(app: &mut App) -> Result<impl Future<Output = ()>, Error> {
        let outgoing_services_names = app.assistant.as_ref().unwrap().outgoing_services_names();
        let mut fs = component::server::ServiceFs::new();
        let mut public = fs.dir("public");
        public.add_fidl_service(Self::spawn_view_provider_server);

        for name in outgoing_services_names {
            public.add_service_at(name, move |channel| {
                Self::pass_connection_to_assistant(channel, name);
                None
            });
        }

        fs.take_and_serve_directory_handle()?;

        Ok(fs.collect())
    }
}
