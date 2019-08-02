// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    geometry::IntSize,
    message::Message,
    view::{ViewAssistantPtr, ViewController, ViewKey},
};
use failure::{bail, format_err, Error, ResultExt};
use fidl::endpoints::{create_endpoints, create_proxy};
use fidl_fuchsia_ui_app::{ViewProviderRequest, ViewProviderRequestStream};
use fidl_fuchsia_ui_policy::PresenterMarker;
use fidl_fuchsia_ui_scenic::{ScenicMarker, ScenicProxy, SessionListenerRequest};
use fidl_fuchsia_ui_views::ViewToken;
use fuchsia_async::{self as fasync, DurationExt, Timer};
use fuchsia_component::{self as component, client::connect_to_service};
use fuchsia_framebuffer::{Frame, FrameBuffer};
use fuchsia_scenic::{Session, SessionPtr, ViewTokenPair};
use fuchsia_zircon::{self as zx, DurationNum};
use futures::{
    channel::oneshot,
    future::{self, FutureExt},
    StreamExt, TryFutureExt, TryStreamExt,
};
use mapped_vmo::Mapping;
use std::{
    cell::RefCell,
    collections::BTreeMap,
    mem,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
};

/// Mode for all views created by this application
#[derive(PartialEq, Debug, Clone, Copy)]
pub enum ViewMode {
    /// This app's views requires Scenic features and should not be run without
    /// Scenic.
    Scenic,
    /// This app's views do all of their rendering with a Canvas and Carnelian should
    /// take care of creating such a canvas for view created by this app.
    Canvas,
}

/// Trait that a mod author must implement. Currently responsible for creating
/// a view assistant when the Fuchsia view framework requests that the mod create
/// a view.
pub trait AppAssistant {
    /// This method is responsible for setting up the AppAssistant implementation.
    /// _It's not clear if this is going to so useful, as anything that isn't
    /// initialized in the creation of the structure implementing AppAssistant
    /// is going to have to be represented as an `Option`, which is awkward._
    fn setup(&mut self) -> Result<(), Error>;

    /// Called when the Fuchsia view system requests that a scenic view be created
    /// for apps running in ViewMode::Scenic
    fn create_view_assistant(
        &mut self,
        _: ViewKey,
        _: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        failure::bail!(
            "Assistant has ViewMode::Scenic but doesn't implement create_view_assistant."
        )
    }

    /// Called when the Fuchsia view system requests that a view be created for
    /// apps running in ViewMode::Canvas.
    fn create_view_assistant_canvas(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        failure::bail!(
            "Assistant has ViewMode::Canvas but doesn't implement create_view_assistant_canvas."
        )
    }

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

    /// Mode for all views created by this app
    fn get_mode(&self) -> ViewMode {
        ViewMode::Scenic
    }
}

pub type TestSender = futures::channel::oneshot::Sender<Result<(), Error>>;

pub type AppAssistantPtr = Box<dyn AppAssistant>;

// This trait exists to keep the scenic implementation and the software
// framebuffer implementations as separate as possible.
// At the moment this abstraction is quite leaky, but it is good
// enough and can be refined with experience.
trait AppStrategy {
    fn supports_scenic(&self) -> bool;
    fn get_scenic_proxy(&self) -> Option<&ScenicProxy>;
    fn get_frame_buffer_mapping(&self) -> Option<&Arc<Mapping>>;
    fn get_frame_buffer_size(&self) -> Option<IntSize>;
    fn get_pixel_size(&self) -> u32;
    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat;
}

type AppStrategyPtr = Box<dyn AppStrategy>;

struct FrameBufferAppStrategy {
    #[allow(unused)]
    frame_buffer: FrameBuffer,
    frame: Frame,
}

impl AppStrategy for FrameBufferAppStrategy {
    fn supports_scenic(&self) -> bool {
        return false;
    }

    fn get_scenic_proxy(&self) -> Option<&ScenicProxy> {
        return None;
    }

    fn get_frame_buffer_mapping(&self) -> Option<&Arc<Mapping>> {
        Some(&self.frame.mapping)
    }

    fn get_frame_buffer_size(&self) -> Option<IntSize> {
        let config = self.frame_buffer.get_config();
        Some(IntSize::new(config.width as i32, config.height as i32))
    }

    fn get_pixel_size(&self) -> u32 {
        let config = self.frame_buffer.get_config();
        config.pixel_size_bytes
    }

    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat {
        let config = self.frame_buffer.get_config();
        config.format
    }
}

// Tries to create a framebuffer. If that fails, assume Scenic is running.
fn create_app_strategy(executor: &mut fasync::Executor) -> Result<AppStrategyPtr, Error> {
    let fb = FrameBuffer::new(None, executor);
    if fb.is_err() {
        let scenic = connect_to_service::<ScenicMarker>()?;
        Ok(Box::new(ScenicAppStrategy { scenic }))
    } else {
        let fb = fb.unwrap();
        let frame = fb.new_frame(executor)?;
        frame.present(&fb)?;
        Ok(Box::new(FrameBufferAppStrategy { frame_buffer: fb, frame }))
    }
}

struct ScenicAppStrategy {
    scenic: ScenicProxy,
}

impl AppStrategy for ScenicAppStrategy {
    fn supports_scenic(&self) -> bool {
        return true;
    }

    fn get_scenic_proxy(&self) -> Option<&ScenicProxy> {
        return Some(&self.scenic);
    }

    fn get_frame_buffer_mapping(&self) -> Option<&Arc<Mapping>> {
        None
    }

    fn get_frame_buffer_size(&self) -> Option<IntSize> {
        None
    }

    fn get_pixel_size(&self) -> u32 {
        4
    }

    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat {
        fuchsia_framebuffer::PixelFormat::Argb8888
    }
}

/// Struct that implements module-wide responsibilities, currently limited
/// to creating views on request.
pub struct App {
    strategy: Option<AppStrategyPtr>,
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
        Ok(RefCell::new(App {
            strategy: None,
            view_controllers: BTreeMap::new(),
            next_key: 0,
            assistant: None,
            messages: Vec::new(),
        }))
    }

    /// Starts an application based on Carnelian. The `assistant` parameter will
    /// be used to create new views when asked to do so by the Fuchsia view system.
    pub fn run(assistant: Box<dyn AppAssistant>) -> Result<(), Error> {
        let executor = fasync::Executor::new().context("Error creating executor")?;
        Self::run_with_executor(assistant, executor)
    }

    fn app_init_common(
        assistant: AppAssistantPtr,
        executor: &mut fasync::Executor,
        app: &mut App,
    ) -> Result<bool, Error> {
        let strat = create_app_strategy(executor)?;
        let supports_scenic = strat.supports_scenic();
        if assistant.get_mode() != ViewMode::Canvas && !supports_scenic {
            bail!("This application requires Scenic but this Fuchsia system doesn't have it.");
        }
        app.strategy.replace(strat);
        app.set_assistant(assistant);
        App::start_services(app)?;
        app.assistant.as_mut().unwrap().setup()?;
        Ok(supports_scenic)
    }

    /// Starts an application based on Carnelian, using an existing executor. The
    /// `assistant` parameter will be used to create new views when asked to
    /// do so by the Fuchsia view system.
    pub fn run_with_executor(
        assistant: AppAssistantPtr,
        mut executor: fasync::Executor,
    ) -> Result<(), Error> {
        let supports_scenic =
            App::with(|app| Self::app_init_common(assistant, &mut executor, app))?;
        if !supports_scenic {
            App::with(|app| {
                let mapping =
                    app.strategy.as_ref().unwrap().get_frame_buffer_mapping().unwrap().clone();
                app.create_view_framebuffer(mapping)
            })?;
        }

        executor.run_singlethreaded(future::pending::<()>());

        Ok(())
    }

    /// Tests an application based on Carnelian. The `assistant` parameter will
    /// be used to create a single new view for testing. The test will run until the
    /// first update call, or until a five second timeout. The Result returned is the
    /// result of the test, an Ok(()) result means the test passed.
    pub fn test(assistant: Box<dyn AppAssistant>) -> Result<(), Error> {
        let mut executor = fasync::Executor::new().context("Error creating executor")?;
        let presenter = connect_to_service::<PresenterMarker>()?;
        let (create_view_sender, create_view_receiver) = oneshot::channel::<Result<(), Error>>();
        let mut token = ViewTokenPair::new().context("ViewTokenPair::new")?;
        let supports_scenic =
            App::with(|app| Self::app_init_common(assistant, &mut executor, app))?;
        if supports_scenic {
            App::with(|app| {
                app.create_view_scenic(token.view_token, Some(create_view_sender))
                    .context("app.create_view")
                    .expect("create_view failed");
                presenter
                    .present_view(&mut token.view_holder_token, None)
                    .expect("present_view failed");
            });
        } else {
            App::with(|app| {
                let mapping =
                    app.strategy.as_ref().unwrap().get_frame_buffer_mapping().unwrap().clone();
                app.create_view_framebuffer(mapping).expect("create_view_framebuffer failed");
            });
        }

        let (timeout_sender, timeout_receiver) = oneshot::channel::<Result<(), Error>>();
        let timeout = Timer::new(5_i64.second().after_now()).map(|_| {
            let timeout_err = format_err!("Carnelian test failed due to timeout");
            timeout_sender.send(Err(timeout_err)).expect("timeout_sender failed");
        });
        fasync::spawn_local(timeout);

        let either = futures::future::select(timeout_receiver, create_view_receiver);
        let resolved = executor.run_singlethreaded(either);
        match resolved.factor_first().0 {
            Err(err) => bail!("Carnelian test got err {:#?}", err),
            Ok(a) => match a {
                Err(err) => bail!("Carnelian test got err {:#?}", err),
                Ok(_) => {}
            },
        }

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

    fn setup_session(&mut self) -> Result<SessionPtr, Error> {
        let (session_listener, session_listener_request) = create_endpoints()?;
        let (session_proxy, session_request) = create_proxy()?;
        let view_id = self.next_key;

        self.get_strategy()
            .get_scenic_proxy()
            .expect("inexplicable failure to get the Scenic proxy in code that can only be invoked when running under Scenic")
            .create_session(session_request, Some(session_listener))?;
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

    // Creates a view assistant for views that are not using the canvas view
    // mode feature.
    fn create_view_assistant(&mut self, session: &SessionPtr) -> Result<ViewAssistantPtr, Error> {
        Ok(self.assistant.as_mut().unwrap().create_view_assistant(self.next_key, session)?)
    }

    // Creates a view assistant for views that are using the canvas view mode feature, either
    // in Scenic or framebuffer mode.
    fn create_view_assistant_canvas(&mut self) -> Result<ViewAssistantPtr, Error> {
        Ok(self.assistant.as_mut().unwrap().create_view_assistant_canvas(self.next_key)?)
    }

    fn get_view_mode(&self) -> ViewMode {
        self.assistant.as_ref().unwrap().get_mode()
    }

    fn create_view_scenic(
        &mut self,
        view_token: ViewToken,
        test_sender: Option<TestSender>,
    ) -> Result<(), Error> {
        let session = self.setup_session()?;
        let view_mode = self.get_view_mode();
        let view_assistant = if view_mode != ViewMode::Canvas {
            self.create_view_assistant(&session)?
        } else {
            self.create_view_assistant_canvas()?
        };
        let mut view_controller = ViewController::new(
            self.next_key,
            view_token,
            view_mode,
            session,
            view_assistant,
            test_sender,
        )?;

        view_controller.setup_animation_mode();

        view_controller.present();
        self.view_controllers.insert(self.next_key, view_controller);
        self.next_key += 1;
        Ok(())
    }

    fn create_view_framebuffer(&mut self, mapping: Arc<Mapping>) -> Result<(), Error> {
        let view_assistant = self.create_view_assistant_canvas()?;
        let strat = self.strategy.as_ref().unwrap();
        let size = strat.get_frame_buffer_size().unwrap();
        let mut view_controller = ViewController::new_with_frame_buffer(
            self.next_key,
            size,
            strat.get_pixel_size(),
            strat.get_pixel_format(),
            mapping,
            view_assistant,
        )?;

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
                        app.create_view_scenic(view_token, None)
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
                        .unwrap_or_else(|e| {
                            eprintln!("error running {} server: {:?}", service_name, e)
                        });
                });
            }
            Err(e) => eprintln!("error asyncifying channel: {:?}", e),
        }
    }

    fn start_services(app: &mut App) -> Result<(), Error> {
        let outgoing_services_names = app.assistant.as_ref().unwrap().outgoing_services_names();
        let mut fs = component::server::ServiceFs::new();
        let mut public = fs.dir("svc");

        if app.scenic_mode() {
            public.add_fidl_service(Self::spawn_view_provider_server);
        }

        for name in outgoing_services_names {
            public.add_service_at(name, move |channel| {
                Self::pass_connection_to_assistant(channel, name);
                None
            });
        }

        match fs.take_and_serve_directory_handle() {
            Err(e) => eprintln!("Error publishing services: {:#}", e),
            Ok(_) => (),
        }

        fasync::spawn_local(fs.collect());

        Ok(())
    }

    fn get_strategy(&mut self) -> &AppStrategyPtr {
        self.strategy.as_ref().expect("Failed to unwrap app strategy")
    }

    fn scenic_mode(&mut self) -> bool {
        self.get_strategy().get_scenic_proxy().is_some()
    }
}
