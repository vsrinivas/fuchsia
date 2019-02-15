// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::view::{NewViewParams, ViewAssistantPtr, ViewController, ViewControllerPtr, ViewKey};
use failure::{bail, Error, ResultExt};
use fidl::endpoints::{RequestStream, ServerEnd, ServiceMarker};
use fidl_fuchsia_ui_app as viewsv2;
use fidl_fuchsia_ui_viewsv1::{
    ViewManagerMarker, ViewManagerProxy, ViewProviderMarker, ViewProviderRequest::CreateView,
    ViewProviderRequestStream,
};
use fidl_fuchsia_ui_viewsv1token::ViewOwnerMarker;
use fuchsia_app::{self as component, client::connect_to_service, server::FdioServer};
use fuchsia_async as fasync;
use fuchsia_scenic::SessionPtr;
use fuchsia_zircon::EventPair;
use futures::{TryFutureExt, TryStreamExt};
use lazy_static::lazy_static;
use parking_lot::Mutex;
use std::{any::Any, collections::BTreeMap, sync::Arc};

/// Trait that a mod author must implement. Currently responsible for creating
/// a view assistant when the Fuchsia view framework requests that the mod create
/// a view.
pub trait AppAssistant: Send {
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

pub type AppAssistantPtr = Mutex<Box<dyn AppAssistant>>;

/// Struct that implements module-wide responsibilties, currently limited
/// to creating views on request.
pub struct App {
    pub(crate) view_manager: ViewManagerProxy,
    view_controllers: BTreeMap<ViewKey, ViewControllerPtr>,
    next_key: ViewKey,
    assistant: Option<AppAssistantPtr>,
}

/// Reference to the singleton app. _This type is likely to change in the future so
/// using this type alias might make for easier forward migration._
pub type AppPtr = Arc<Mutex<App>>;

lazy_static! {
    /// Singleton reference to the running application
    pub static ref APP: AppPtr = App::new().expect("Failed to create app");
}

impl App {
    fn new() -> Result<AppPtr, Error> {
        let view_manager = connect_to_service::<ViewManagerMarker>()?;
        Ok(Arc::new(Mutex::new(App {
            view_manager,
            view_controllers: BTreeMap::new(),
            next_key: 0,
            assistant: None,
        })))
    }

    /// Starts an application based on Carnelian. The `assistant` parameter will
    /// be used to create new views when asked to do so by the Fuchsia view system.
    pub fn run(assistant: Box<AppAssistant>) -> Result<(), Error> {
        let mut executor = fasync::Executor::new().context("Error creating executor")?;

        APP.lock().set_assistant(Mutex::new(assistant));

        let fut = Self::start_services(&APP)?;

        APP.lock().assistant.as_ref().unwrap().lock().setup()?;

        executor.run_singlethreaded(fut)?;

        Ok(())
    }

    fn set_assistant(&mut self, assistant: AppAssistantPtr) {
        self.assistant = Some(assistant);
    }

    /// Method for app and view assistants to use from closures in
    /// order to reconnect with a specific `ViewController`.
    pub fn find_view_controller(&self, key: ViewKey) -> Option<&ViewControllerPtr> {
        self.view_controllers.get(&key)
    }

    /// Send a message to a specific view controller. Messages not handled by the ViewController
    /// will be forwarded to the `ViewControllerAssistant`.
    pub fn send_message(&mut self, target: ViewKey, msg: &Any) {
        if let Some(view) = self.view_controllers.get(&target) {
            view.lock().send_message(msg);
        }
    }

    pub(crate) fn create_view_assistant(
        &mut self,
        session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(self.assistant.as_ref().unwrap().lock().create_view_assistant(session)?)
    }

    fn create_view(&mut self, req: ServerEnd<ViewOwnerMarker>) -> Result<(), Error> {
        let view_controller = ViewController::new(self, NewViewParams::V1(req), self.next_key)?;
        self.view_controllers.insert(self.next_key, view_controller);
        self.next_key += 1;
        Ok(())
    }

    fn create_view2(&mut self, view_token: EventPair) -> Result<(), Error> {
        let view_controller =
            ViewController::new(self, NewViewParams::V2(view_token), self.next_key)?;
        self.view_controllers.insert(self.next_key, view_controller);
        self.next_key += 1;
        Ok(())
    }

    fn spawn_view_provider_server(chan: fasync::Channel, app: &AppPtr) {
        let app = app.clone();
        fasync::spawn(
            ViewProviderRequestStream::from_channel(chan)
                .try_for_each(move |req| {
                    let CreateView { view_owner, .. } = req;
                    app.lock()
                        .create_view(view_owner)
                        .unwrap_or_else(|e| eprintln!("create_view error: {:?}", e));
                    futures::future::ready(Ok(()))
                })
                .unwrap_or_else(|e| eprintln!("error running view_provider server: {:?}", e)),
        )
    }

    fn spawn_v2_view_provider_server(chan: fasync::Channel, app: &AppPtr) {
        let app = app.clone();
        fasync::spawn(
            viewsv2::ViewProviderRequestStream::from_channel(chan)
                .try_for_each(move |req| {
                    let viewsv2::ViewProviderRequest::CreateView { token, .. } = req;
                    app.lock()
                        .create_view2(token)
                        .unwrap_or_else(|e| eprintln!("create_view2 error: {:?}", e));
                    futures::future::ready(Ok(()))
                })
                .unwrap_or_else(|e| eprintln!("error running V2 view_provider server: {:?}", e)),
        )
    }

    fn pass_connection_to_assistant(
        channel: fasync::Channel,
        service_name: &'static str,
        app: &AppPtr,
    ) {
        app.lock()
            .assistant
            .as_ref()
            .unwrap()
            .lock()
            .handle_service_connection_request(service_name, channel)
            .unwrap_or_else(|e| eprintln!("error running {} server: {:?}", service_name, e));
    }

    fn start_services(app: &AppPtr) -> Result<FdioServer, Error> {
        let outgoing_services_names =
            app.lock().assistant.as_ref().unwrap().lock().outgoing_services_names();
        let app_view_provider = app.clone();
        let app_view_provider2 = app.clone();
        let services_server = component::server::ServicesServer::new();
        let mut services_server = services_server
            .add_service((ViewProviderMarker::NAME, move |channel| {
                Self::spawn_view_provider_server(channel, &app_view_provider);
            }))
            .add_service((viewsv2::ViewProviderMarker::NAME, move |channel| {
                Self::spawn_v2_view_provider_server(channel, &app_view_provider2);
            }));

        for name in outgoing_services_names {
            let app_server = app.clone();
            services_server = services_server.add_service((name, move |channel| {
                Self::pass_connection_to_assistant(channel, name, &app_server);
            }));
        }

        Ok(services_server.start()?)
    }
}
