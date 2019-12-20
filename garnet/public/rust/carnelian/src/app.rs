// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    geometry::IntSize,
    message::Message,
    view::{ViewAssistantPtr, ViewController, ViewKey},
};
use async_trait::async_trait;
use failure::{bail, format_err, Error, ResultExt};
use fidl::endpoints::{create_endpoints, create_proxy};
use fidl_fuchsia_ui_app::{ViewProviderRequest, ViewProviderRequestStream};
use fidl_fuchsia_ui_policy::PresenterMarker;
use fidl_fuchsia_ui_scenic::{ScenicMarker, ScenicProxy, SessionListenerRequest};
use fidl_fuchsia_ui_views::ViewToken;
use fuchsia_async::{self as fasync, DurationExt, Timer};
use fuchsia_component::{self as component, client::connect_to_service};
use fuchsia_framebuffer::{FrameBuffer, FrameUsage, VSyncMessage};
use fuchsia_scenic::{Session, SessionPtr, ViewTokenPair};
use fuchsia_zircon::{self as zx, DurationNum};
use futures::{
    channel::mpsc::{unbounded, UnboundedSender},
    future::{Either, Future},
    StreamExt, TryFutureExt, TryStreamExt,
};
use std::{cell::RefCell, collections::BTreeMap, pin::Pin, rc::Rc};

pub type LocalBoxFuture<'a, T> = Pin<Box<dyn Future<Output = T> + 'a>>;

/// Mode for all views created by this application
#[derive(PartialEq, Debug, Clone, Copy)]
pub enum ViewMode {
    /// This app's views requires Scenic features and should not be run without
    /// Scenic.
    Scenic,
    /// This app's views do all of their rendering with a Canvas and Carnelian should
    /// take care of creating such a canvas for view created by this app.
    Canvas,
    /// This app's views requires an image pipe and Carnelian should
    /// take care of creating a buffer collection for view created by this app.
    ImagePipe,
}

#[derive(Clone)]
pub struct AppContext {
    sender: UnboundedSender<MessageInternal>,
}

impl AppContext {
    pub fn queue_message(&self, target: ViewKey, message: Message) {
        self.sender
            .unbounded_send(MessageInternal::TargetedMessage(target, message))
            .expect("AppContext::queue_message - unbounded_send");
    }

    pub fn new_for_testing_purposes_only() -> AppContext {
        let (internal_sender, _) = unbounded::<MessageInternal>();
        AppContext { sender: internal_sender }
    }
}

fn make_app_assistant_fut<T: AppAssistant + Default + 'static>(
    _: &AppContext,
) -> LocalBoxFuture<'_, Result<AppAssistantPtr, Error>> {
    let f = async move {
        let assistant = Box::new(T::default());
        Ok::<AppAssistantPtr, Error>(assistant)
    };
    Box::pin(f)
}

pub fn make_app_assistant<T: AppAssistant + Default + 'static>() -> AssistantCreatorFunc {
    Box::new(make_app_assistant_fut::<T>)
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

    /// Called when the Fuchsia view system requests that a view be created for
    /// apps running in ViewMode::ImagePipe.
    fn create_view_assistant_image_pipe(
        &mut self,
        _: ViewKey,
        _fb: FrameBufferPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        failure::bail!(
            "Assistant has ViewMode::ImagePipe but doesn't implement create_view_assistant_image_pipe."
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

    /// Return true to indicate that this apps view assistants will
    /// arrange to have the frame buffer's wait event signaled
    fn signals_wait_event(&self) -> bool {
        return false;
    }
}

pub type AppAssistantPtr = Box<dyn AppAssistant>;

// This trait exists to keep the scenic implementation and the software
// framebuffer implementations as separate as possible.
// At the moment this abstraction is quite leaky, but it is good
// enough and can be refined with experience.
#[async_trait(?Send)]
trait AppStrategy {
    fn supports_scenic(&self) -> bool;
    fn get_scenic_proxy(&self) -> Option<&ScenicProxy>;
    fn get_frame_buffer(&self) -> Option<FrameBufferPtr> {
        None
    }
    fn get_frame_buffer_size(&self) -> Option<IntSize>;
    fn get_pixel_size(&self) -> u32;
    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat;
    fn get_linear_stride_bytes(&self) -> u32;
    async fn post_setup(
        &mut self,
        _pixel_format: fuchsia_framebuffer::PixelFormat,
    ) -> Result<(), Error>;
}

type AppStrategyPtr = Box<dyn AppStrategy>;

/// Reference to FrameBuffer.
pub type FrameBufferPtr = Rc<RefCell<FrameBuffer>>;

struct FrameBufferAppStrategy {
    #[allow(unused)]
    frame_buffer: FrameBufferPtr,
}

const FRAME_COUNT: usize = 2;

#[async_trait(?Send)]
impl AppStrategy for FrameBufferAppStrategy {
    fn supports_scenic(&self) -> bool {
        return false;
    }

    fn get_scenic_proxy(&self) -> Option<&ScenicProxy> {
        return None;
    }

    fn get_frame_buffer_size(&self) -> Option<IntSize> {
        let config = self.frame_buffer.borrow().get_config();
        Some(IntSize::new(config.width as i32, config.height as i32))
    }

    fn get_pixel_size(&self) -> u32 {
        let config = self.frame_buffer.borrow().get_config();
        config.pixel_size_bytes
    }

    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat {
        let config = self.frame_buffer.borrow().get_config();
        config.format
    }

    fn get_linear_stride_bytes(&self) -> u32 {
        let config = self.frame_buffer.borrow().get_config();
        config.linear_stride_bytes() as u32
    }

    fn get_frame_buffer(&self) -> Option<FrameBufferPtr> {
        Some(self.frame_buffer.clone())
    }

    async fn post_setup(
        &mut self,
        pixel_format: fuchsia_framebuffer::PixelFormat,
    ) -> Result<(), Error> {
        let mut fb = self.frame_buffer.borrow_mut();
        fb.allocate_frames(FRAME_COUNT, pixel_format).await?;
        Ok(())
    }
}

// Tries to create a framebuffer. If that fails, assume Scenic is running.
async fn create_app_strategy(
    assistant: &AppAssistantPtr,
    internal_sender: &UnboundedSender<MessageInternal>,
) -> Result<AppStrategyPtr, Error> {
    let usage =
        if assistant.get_mode() == ViewMode::ImagePipe { FrameUsage::Gpu } else { FrameUsage::Cpu };
    let (sender, mut receiver) = unbounded::<VSyncMessage>();
    let fb = FrameBuffer::new(usage, None, Some(sender)).await;
    if fb.is_err() {
        let scenic = connect_to_service::<ScenicMarker>()?;
        Ok::<AppStrategyPtr, Error>(Box::new(ScenicAppStrategy { scenic }))
    } else {
        let fb = fb.unwrap();
        let internal_sender = internal_sender.clone();

        // TODO: improve scheduling of updates
        fasync::spawn_local(
            async move {
                while let Some(_) = receiver.next().await {
                    internal_sender
                        .unbounded_send(MessageInternal::UpdateAllViews)
                        .expect("unbounded_send");
                }
                Ok(())
            }
            .unwrap_or_else(|e: failure::Error| {
                println!("error {:#?}", e);
            }),
        );

        Ok::<AppStrategyPtr, Error>(Box::new(FrameBufferAppStrategy {
            frame_buffer: Rc::new(RefCell::new(fb)),
        }))
    }
}

struct ScenicAppStrategy {
    scenic: ScenicProxy,
}

#[async_trait(?Send)]
impl AppStrategy for ScenicAppStrategy {
    fn supports_scenic(&self) -> bool {
        return true;
    }

    fn get_scenic_proxy(&self) -> Option<&ScenicProxy> {
        return Some(&self.scenic);
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

    fn get_linear_stride_bytes(&self) -> u32 {
        0
    }

    async fn post_setup(&mut self, _: fuchsia_framebuffer::PixelFormat) -> Result<(), Error> {
        Ok(())
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
    sender: Option<UnboundedSender<MessageInternal>>,
}

pub(crate) enum MessageInternal {
    ServiceConnection(zx::Channel, &'static str),
    CreateView(ViewToken),
    ScenicEvent(Vec<fidl_fuchsia_ui_scenic::Event>, ViewKey),
    ScenicPresentDone(ViewKey),
    Update(ViewKey),
    UpdateAllViews,
    ImageFreed(ViewKey, u64, u32),
    TargetedMessage(ViewKey, Message),
}

pub type AssistantCreator<'a> = LocalBoxFuture<'a, Result<AppAssistantPtr, Error>>;
pub type AssistantCreatorFunc = Box<dyn Fn(&AppContext) -> AssistantCreator<'_>>;

impl App {
    fn new(sender: Option<UnboundedSender<MessageInternal>>) -> App {
        App {
            strategy: None,
            view_controllers: BTreeMap::new(),
            next_key: 0,
            assistant: None,
            messages: Vec::new(),
            sender,
        }
    }

    /// Starts an application based on Carnelian. The `assistant` parameter will
    /// be used to create new views when asked to do so by the Fuchsia view system.
    pub fn run(assistant_creator_func: AssistantCreatorFunc) -> Result<(), Error> {
        let mut executor = fasync::Executor::new().context("Error creating executor")?;
        let (internal_sender, mut internal_receiver) = unbounded::<MessageInternal>();
        let f = async {
            let app_context = AppContext { sender: internal_sender.clone() };
            let assistant_creator = assistant_creator_func(&app_context);
            let assistant = assistant_creator.await?;
            let mut app = App::new(Some(internal_sender));
            let supports_scenic = app.app_init_common_async(assistant).await?;
            if !supports_scenic {
                app.create_view_framebuffer_async().await?;
            }
            while let Some(message) = internal_receiver.next().await {
                app.handle_message(message).await.expect("handle_message failed");
            }
            Ok::<(), Error>(())
        };
        executor.run_singlethreaded(f)?;
        Ok(())
    }

    async fn handle_message(&mut self, message: MessageInternal) -> Result<(), Error> {
        match message {
            MessageInternal::ServiceConnection(channel, service_name) => {
                match fasync::Channel::from_channel(channel) {
                    Ok(channel) => {
                        self.assistant
                            .as_mut()
                            .unwrap()
                            .handle_service_connection_request(service_name, channel)
                            .unwrap_or_else(|e| {
                                eprintln!("error running {} server: {:?}", service_name, e)
                            });
                    }
                    Err(e) => eprintln!("error asyncifying channel: {:?}", e),
                }
            }
            MessageInternal::CreateView(view_token) => {
                self.create_view_scenic(view_token).await?;
            }
            MessageInternal::ScenicEvent(events, view_id) => {
                self.handle_session_event(view_id, events);
            }
            MessageInternal::ScenicPresentDone(view_id) => {
                let view = self.get_view(view_id);
                view.present_done();
            }
            MessageInternal::Update(view_id) => {
                let view = self.get_view(view_id);
                view.update_async().await;
            }
            MessageInternal::UpdateAllViews => self.update_all_views(),
            MessageInternal::ImageFreed(view_id, image_id, collection_id) => {
                self.image_freed(view_id, image_id, collection_id)
            }
            MessageInternal::TargetedMessage(view_id, message) => {
                let view = self.get_view(view_id);
                view.send_message(message);
            }
        }
        Ok(())
    }

    async fn app_init_common_async(&mut self, assistant: AppAssistantPtr) -> Result<bool, Error> {
        let strat = create_app_strategy(&assistant, self.sender.as_ref().expect("sender")).await?;
        let supports_scenic = strat.supports_scenic();
        if assistant.get_mode() == ViewMode::Scenic && !supports_scenic {
            bail!("This application requires Scenic but this Fuchsia system doesn't have it.");
        }
        self.strategy.replace(strat);
        self.set_assistant(assistant);
        self.assistant.as_mut().expect("assistant").setup().context("app setup")?;
        self.start_services_async()?;
        Ok(supports_scenic)
    }

    /// Tests an application based on Carnelian. The `assistant` parameter will
    /// be used to create a single new view for testing. The test will run until the
    /// first update call, or until a five second timeout. The Result returned is the
    /// result of the test, an Ok(()) result means the test passed.
    pub fn test(assistant: AppAssistantPtr) -> Result<(), Error> {
        let mut executor = fasync::Executor::new().context("Error creating executor")?;
        let presenter = connect_to_service::<PresenterMarker>()?;
        let (internal_sender, mut internal_receiver) = unbounded::<MessageInternal>();
        let f = async {
            let mut token = ViewTokenPair::new().context("ViewTokenPair::new")?;
            let mut app = App::new(Some(internal_sender));
            let mut frame_count = 0;
            let supports_scenic = app.app_init_common_async(assistant).await?;
            if supports_scenic {
                app.create_view_scenic(token.view_token)
                    .await
                    .context("app.create_view")
                    .expect("create_view failed");
                presenter
                    .present_view(&mut token.view_holder_token, None)
                    .expect("present_view failed");
            } else {
                app.create_view_framebuffer_async().await?;
            }
            loop {
                let timeout = Timer::new(500_i64.millis().after_now());
                let either = futures::future::select(timeout, internal_receiver.next());
                let resolved = either.await;
                match resolved {
                    Either::Left(_) => {
                        return Err(format_err!(
                            "Carnelian test got timeout before seeing 10 frames"
                        ));
                    }
                    Either::Right((right_result, _)) => {
                        let message = right_result.expect("message");
                        match message {
                            MessageInternal::Update(_) => {
                                frame_count += 1;
                            }
                            MessageInternal::UpdateAllViews => {
                                frame_count += 1;
                            }
                            _ => (),
                        }
                        app.handle_message(message).await.expect("handle_message failed");
                        if frame_count > 10 {
                            break;
                        }
                    }
                }
            }
            Ok::<(), Error>(())
        };

        executor.run_singlethreaded(f)?;

        Ok(())
    }

    fn get_view(&mut self, view_key: ViewKey) -> &mut ViewController {
        if let Some(view) = self.view_controllers.get_mut(&view_key) {
            view
        } else {
            panic!("Could not find view controller for {}", view_key);
        }
    }

    fn set_assistant(&mut self, assistant: AppAssistantPtr) {
        self.assistant = Some(assistant);
    }

    fn update_all_views(&mut self) {
        for (_, view_controller) in &mut self.view_controllers {
            view_controller.update();
        }
    }

    /// Send a message to a specific view controller. Messages not handled by the ViewController
    /// will be forwarded to the `ViewControllerAssistant`.
    pub fn queue_message(&mut self, target: ViewKey, msg: Message) {
        self.messages.push((target, msg));
    }

    fn handle_session_event(
        &mut self,
        view_id: ViewKey,
        events: Vec<fidl_fuchsia_ui_scenic::Event>,
    ) {
        let view = self.get_view(view_id);
        view.handle_session_events(events);
    }

    fn setup_session(&mut self) -> Result<SessionPtr, Error> {
        let (session_listener, session_listener_request) = create_endpoints()?;
        let (session_proxy, session_request) = create_proxy()?;
        let view_id = self.next_key;

        self.get_strategy()
            .get_scenic_proxy()
            .expect("inexplicable failure to get the Scenic proxy in code that can only be invoked when running under Scenic")
            .create_session(session_request, Some(session_listener))?;
        let sender = self.sender.as_ref().expect("sender").clone();
        fasync::spawn_local(
            session_listener_request
                .into_stream()?
                .map_ok(move |request| match request {
                    SessionListenerRequest::OnScenicEvent { events, .. } => {
                        sender
                            .unbounded_send(MessageInternal::ScenicEvent(events, view_id))
                            .expect("unbounded_send session event");
                    }
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

    fn get_signals_wait_event(&self) -> bool {
        self.assistant.as_ref().unwrap().signals_wait_event()
    }

    async fn create_view_scenic(&mut self, view_token: ViewToken) -> Result<(), Error> {
        let session = self.setup_session()?;
        let view_mode = self.get_view_mode();
        let view_assistant = match view_mode {
            ViewMode::Scenic => self.create_view_assistant(&session)?,
            ViewMode::Canvas => self.create_view_assistant_canvas()?,
            ViewMode::ImagePipe => bail!("ImagePipe mode is not yet supported with Scenic."),
        };
        let mut view_controller = ViewController::new(
            self.next_key,
            view_token,
            view_mode,
            session,
            view_assistant,
            self.sender.as_ref().expect("sender").clone(),
        )
        .await?;

        view_controller.setup_animation_mode();

        view_controller.present();
        self.view_controllers.insert(self.next_key, view_controller);
        self.next_key += 1;
        Ok(())
    }

    // Creates a view assistant for views that are using the image pipe view
    // mode feature, either in Scenic or framebuffer mode.
    fn create_view_assistant_image_pipe(&mut self) -> Result<ViewAssistantPtr, Error> {
        let strat = self.strategy.as_ref().unwrap();
        let fb = strat.get_frame_buffer().unwrap();
        let view =
            self.assistant.as_mut().unwrap().create_view_assistant_image_pipe(self.next_key, fb)?;
        Ok(view)
    }

    async fn create_view_framebuffer_async(&mut self) -> Result<(), Error> {
        let view_mode = self.get_view_mode();
        let signals_wait_event = self.get_signals_wait_event();
        let view_assistant = if view_mode == ViewMode::ImagePipe {
            self.create_view_assistant_image_pipe()?
        } else {
            self.create_view_assistant_canvas()?
        };
        let strat = self.strategy.as_mut().unwrap();
        let pixel_format = if view_mode == ViewMode::ImagePipe {
            view_assistant.get_pixel_format()
        } else {
            strat.get_pixel_format()
        };
        strat.post_setup(pixel_format).await?;

        let size = strat.get_frame_buffer_size().unwrap();
        let mut view_controller = ViewController::new_with_frame_buffer(
            self.next_key,
            size,
            strat.get_pixel_size(),
            pixel_format,
            strat.get_linear_stride_bytes(),
            view_assistant,
            self.sender.as_ref().expect("sender").clone(),
            strat.get_frame_buffer().unwrap(),
            signals_wait_event,
        )?;

        // For framebuffer apps, always use vsync to drive update
        // TODO: limit update rate in update if the requested refresh
        // rate does not require a draw on every vsync.

        view_controller.present();
        self.view_controllers.insert(self.next_key, view_controller);
        self.next_key += 1;
        self.update_all_views();
        Ok(())
    }

    fn start_services_async(self: &mut App) -> Result<(), Error> {
        let outgoing_services_names =
            self.assistant.as_ref().expect("assistant").outgoing_services_names();
        let mut fs = component::server::ServiceFs::new_local();
        let mut public = fs.dir("svc");

        if self.scenic_mode() {
            let sender = self.sender.as_ref().expect("sender").clone();
            let f = move |stream: ViewProviderRequestStream| {
                let sender = sender.clone();
                fasync::spawn_local(
                    stream
                        .try_for_each(move |req| {
                            let ViewProviderRequest::CreateView { token, .. } = req;
                            let view_token = ViewToken { value: token };
                            sender
                                .unbounded_send(MessageInternal::CreateView(view_token))
                                .expect("send");
                            futures::future::ready(Ok(()))
                        })
                        .unwrap_or_else(|e| {
                            eprintln!("error running ViewProvider server: {:?}", e)
                        }),
                )
            };
            public.add_fidl_service(f);
        }

        for name in outgoing_services_names {
            let sender = self.sender.as_ref().expect("sender").clone();
            public.add_service_at(name, move |channel| {
                sender
                    .unbounded_send(MessageInternal::ServiceConnection(channel, name))
                    .expect("unbounded_send");
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

    pub(crate) fn image_freed(&mut self, view_id: ViewKey, image_id: u64, collection_id: u32) {
        let view = self.get_view(view_id);
        view.image_freed(image_id, collection_id);
    }
}
