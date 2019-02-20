// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::view::{ViewAssistantPtr, ViewController, ViewKey};
use failure::{bail, Error, ResultExt};
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fuchsia_ui_app as viewsv2;
use fidl_fuchsia_ui_gfx as gfx;
use fidl_fuchsia_ui_scenic::{ScenicMarker, ScenicProxy};
use fidl_fuchsia_ui_viewsv1::{
    ViewManagerMarker, ViewManagerProxy, ViewProviderMarker, ViewProviderRequest::CreateView,
    ViewProviderRequestStream,
};
use fuchsia_app::{self as component, client::connect_to_service, server::FdioServer};
use fuchsia_async as fasync;
use fuchsia_scenic::SessionPtr;
use fuchsia_zircon as zx;
use futures::{TryFutureExt, TryStreamExt};
use std::{
    any::Any,
    cell::RefCell,
    collections::BTreeMap,
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
    fn create_view_assistant(&mut self, session: &SessionPtr) -> Result<ViewAssistantPtr, Error>;

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

/// Struct that implements module-wide responsibilties, currently limited
/// to creating views on request.
pub struct App {
    pub(crate) scenic: ScenicProxy,
    pub(crate) view_manager: ViewManagerProxy,
    view_controllers: BTreeMap<ViewKey, ViewController>,
    next_key: ViewKey,
    assistant: Option<AppAssistantPtr>,
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
        let view_manager = connect_to_service::<ViewManagerMarker>()?;
        Ok(RefCell::new(App {
            scenic,
            view_manager,
            view_controllers: BTreeMap::new(),
            next_key: 0,
            assistant: None,
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

        executor.run_singlethreaded(fut)?;

        Ok(())
    }

    /// Function to get a mutable reference to the singleton app struct, useful
    /// in callbacks.
    pub fn with<F, R>(f: F) -> R
    where
        F: FnOnce(&mut App) -> R,
    {
        APP.with(|app| {
            let mut app_ref = app.borrow_mut();
            f(&mut app_ref)
        })
    }

    fn set_assistant(&mut self, assistant: AppAssistantPtr) {
        self.assistant = Some(assistant);
    }

    /// Function to get a mutable reference to a view controller for a particular
    /// view, by view key. It is a fatal error to pass a view key that doesn't
    /// have a corresponding view controller.
    pub fn with_view<F>(&mut self, key: ViewKey, f: F)
    where
        F: FnOnce(&mut ViewController),
    {
        if let Some(view) = self.view_controllers.get_mut(&key) {
            f(view)
        } else {
            panic!("Could not find view controller for {}", key);
        }
    }

    /// Send a message to a specific view controller. Messages not handled by the ViewController
    /// will be forwarded to the `ViewControllerAssistant`.
    pub fn send_message(&mut self, target: ViewKey, msg: &Any) {
        if let Some(view) = self.view_controllers.get_mut(&target) {
            view.send_message(msg);
        }
    }

    pub(crate) fn create_view_assistant(
        &mut self,
        session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(self.assistant.as_mut().unwrap().create_view_assistant(session)?)
    }

    fn create_view(&mut self, view_token: gfx::ExportToken) -> Result<(), Error> {
        let view_controller = ViewController::new(self, view_token, self.next_key)?;
        self.view_controllers.insert(self.next_key, view_controller);
        self.next_key += 1;
        Ok(())
    }

    fn spawn_view_provider_server(chan: fasync::Channel) {
        fasync::spawn_local(
            ViewProviderRequestStream::from_channel(chan)
                .try_for_each(move |req| {
                    let CreateView { view_owner, .. } = req;
                    let view_token = gfx::ExportToken {
                        value: zx::EventPair::from(zx::Handle::from(view_owner.into_channel())),
                    };

                    App::with(|app| {
                        app.create_view(view_token)
                            .unwrap_or_else(|e| eprintln!("create_view error: {:?}", e));
                    });
                    futures::future::ready(Ok(()))
                })
                .unwrap_or_else(|e| eprintln!("error running view_provider server: {:?}", e)),
        )
    }

    fn spawn_v2_view_provider_server(chan: fasync::Channel) {
        fasync::spawn_local(
            viewsv2::ViewProviderRequestStream::from_channel(chan)
                .try_for_each(move |req| {
                    let viewsv2::ViewProviderRequest::CreateView { token, .. } = req;
                    let view_token = gfx::ExportToken { value: token };
                    App::with(|app| {
                        app.create_view(view_token)
                            .unwrap_or_else(|e| eprintln!("create_view2 error: {:?}", e));
                    });
                    futures::future::ready(Ok(()))
                })
                .unwrap_or_else(|e| eprintln!("error running V2 view_provider server: {:?}", e)),
        )
    }

    fn pass_connection_to_assistant(channel: fasync::Channel, service_name: &'static str) {
        App::with(|app| {
            app.assistant
                .as_mut()
                .unwrap()
                .handle_service_connection_request(service_name, channel)
                .unwrap_or_else(|e| eprintln!("error running {} server: {:?}", service_name, e));
        });
    }

    fn start_services(app: &mut App) -> Result<FdioServer, Error> {
        let outgoing_services_names = app.assistant.as_ref().unwrap().outgoing_services_names();
        let services_server = component::server::ServicesServer::new();
        let mut services_server = services_server
            .add_service((ViewProviderMarker::NAME, move |channel| {
                Self::spawn_view_provider_server(channel);
            }))
            .add_service((viewsv2::ViewProviderMarker::NAME, move |channel| {
                Self::spawn_v2_view_provider_server(channel);
            }));

        for name in outgoing_services_names {
            services_server = services_server.add_service((name, move |channel| {
                Self::pass_connection_to_assistant(channel, name);
            }));
        }

        Ok(services_server.start()?)
    }
}
