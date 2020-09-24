// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::strategies::base::{create_app_strategy, AppStrategyPtr},
    geometry::Size,
    input::DeviceId,
    message::Message,
    view::{strategies::base::ViewStrategyParams, ViewAssistantPtr, ViewController, ViewKey},
};
use anyhow::{format_err, Context as _, Error};
use fidl_fuchsia_input_report as hid_input_report;
use fuchsia_async::{self as fasync, DurationExt, Timer};
use fuchsia_framebuffer::FrameBuffer;
use fuchsia_zircon::{self as zx, Duration, DurationNum, Time};
use futures::{
    channel::mpsc::{unbounded, UnboundedSender},
    future::{Either, Future},
    StreamExt,
};
use std::{cell::RefCell, collections::BTreeMap, pin::Pin, rc::Rc};

mod strategies;

pub type LocalBoxFuture<'a, T> = Pin<Box<dyn Future<Output = T> + 'a>>;

#[derive(PartialEq, Debug, Default, Copy, Clone)]
pub struct RenderOptions {
    pub use_spinel: bool,
}

pub(crate) type InternalSender = UnboundedSender<MessageInternal>;

pub(crate) const FIRST_VIEW_KEY: ViewKey = 100;

#[derive(Clone)]
pub struct AppContext {
    sender: InternalSender,
}

impl AppContext {
    pub fn queue_message(&self, target: ViewKey, message: Message) {
        self.sender
            .unbounded_send(MessageInternal::TargetedMessage(target, message))
            .expect("AppContext::queue_message - unbounded_send");
    }

    pub fn request_render(&self, target: ViewKey) {
        self.sender
            .unbounded_send(MessageInternal::RequestRender(target))
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

    /// Called when the Fuchsia view system requests that a view be created, or once at startup
    /// when running without Scenic.
    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error>;

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
        return Err(format_err!("handle_service_connection_request not implemented"));
    }

    /// Mode options for rendering
    fn get_render_options(&self) -> RenderOptions {
        RenderOptions::default()
    }
}

pub type AppAssistantPtr = Box<dyn AppAssistant>;

/// Reference to FrameBuffer.
pub type FrameBufferPtr = Rc<RefCell<FrameBuffer>>;

/// Struct that implements module-wide responsibilities, currently limited
/// to creating views on request.
pub struct App {
    strategy: AppStrategyPtr,
    view_controllers: BTreeMap<ViewKey, ViewController>,
    next_key: ViewKey,
    assistant: AppAssistantPtr,
    messages: Vec<(ViewKey, Message)>,
    sender: InternalSender,
}

pub(crate) enum MessageInternal {
    ServiceConnection(zx::Channel, &'static str),
    CreateView(ViewStrategyParams),
    MetricsChanged(ViewKey, Size),
    SizeChanged(ViewKey, Size),
    ScenicInputEvent(ViewKey, fidl_fuchsia_ui_input::InputEvent),
    ScenicPresentSubmitted(ViewKey, fidl_fuchsia_scenic_scheduling::FuturePresentationTimes),
    ScenicPresentDone(ViewKey, fidl_fuchsia_scenic_scheduling::FramePresentedInfo),
    Focus(ViewKey),
    RequestRender(ViewKey),
    Render(ViewKey),
    RenderAllViews,
    ImageFreed(ViewKey, u64, u32),
    HandleVSyncParametersChanged(Time, Duration, u64),
    TargetedMessage(ViewKey, Message),
    RegisterDevice(DeviceId, hid_input_report::DeviceDescriptor),
    InputReport(DeviceId, ViewKey, hid_input_report::InputReport),
}

pub type AssistantCreator<'a> = LocalBoxFuture<'a, Result<AppAssistantPtr, Error>>;
pub type AssistantCreatorFunc = Box<dyn Fn(&AppContext) -> AssistantCreator<'_>>;

impl App {
    fn new(sender: InternalSender, strategy: AppStrategyPtr, assistant: AppAssistantPtr) -> App {
        App {
            strategy,
            view_controllers: BTreeMap::new(),
            next_key: FIRST_VIEW_KEY,
            assistant,
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
            let strat = create_app_strategy(&assistant, FIRST_VIEW_KEY, &internal_sender).await?;
            let mut app = App::new(internal_sender, strat, assistant);
            app.app_init_common().await?;
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
                            .handle_service_connection_request(service_name, channel)
                            .unwrap_or_else(|e| {
                                eprintln!("error running {} server: {:?}", service_name, e)
                            });
                    }
                    Err(e) => eprintln!("error asyncifying channel: {:?}", e),
                }
            }
            MessageInternal::CreateView(params) => self.create_view_with_params(params).await?,
            MessageInternal::MetricsChanged(view_id, metrics) => {
                let view = self.get_view(view_id);
                view.handle_metrics_changed(metrics);
            }
            MessageInternal::SizeChanged(view_id, new_size) => {
                let view = self.get_view(view_id);
                view.handle_size_changed(new_size).await;
            }
            MessageInternal::ScenicInputEvent(view_id, event) => {
                let view = self.get_view(view_id);
                view.handle_scenic_input_event(event);
            }
            MessageInternal::ScenicPresentSubmitted(view_id, info) => {
                let view = self.get_view(view_id);
                view.present_submitted(info);
            }
            MessageInternal::ScenicPresentDone(view_id, info) => {
                let view = self.get_view(view_id);
                view.present_done(info);
            }
            MessageInternal::Focus(view_id) => {
                let view = self.get_view(view_id);
                view.focus(true);
            }
            MessageInternal::RequestRender(view_id) => {
                let view = self.get_view(view_id);
                view.request_render();
            }
            MessageInternal::Render(view_id) => {
                let view = self.get_view(view_id);
                view.render().await;
            }
            MessageInternal::RenderAllViews => self.render_all_views(),
            MessageInternal::ImageFreed(view_id, image_id, collection_id) => {
                self.image_freed(view_id, image_id, collection_id)
            }
            MessageInternal::HandleVSyncParametersChanged(phase, interval, cookie) => {
                self.handle_vsync_parameters_changed(phase, interval);
                self.handle_vsync_cookie(cookie);
            }
            MessageInternal::TargetedMessage(view_id, message) => {
                let view = self.get_view(view_id);
                view.send_message(message);
            }
            MessageInternal::RegisterDevice(device_id, device_descriptor) => {
                self.strategy.handle_register_input_device(&device_id, &device_descriptor);
            }
            MessageInternal::InputReport(device_id, view_id, input_report) => {
                let input_events = self.strategy.handle_input_report(&device_id, &input_report);

                let view = self.get_view(view_id);
                view.handle_input_events(input_events);
            }
        }
        Ok(())
    }

    async fn app_init_common(&mut self) -> Result<(), Error> {
        self.assistant.setup().context("app setup")?;
        self.start_services()?;
        Ok(())
    }

    /// Tests an application based on Carnelian. The `assistant` parameter will
    /// be used to create a single new view for testing. The test will run until the
    /// first update call, or until a five second timeout. The Result returned is the
    /// result of the test, an Ok(()) result means the test passed.
    pub fn test(assistant_creator_func: AssistantCreatorFunc) -> Result<(), Error> {
        let mut executor = fasync::Executor::new().context("Error creating executor")?;
        let (internal_sender, mut internal_receiver) = unbounded::<MessageInternal>();
        let f = async {
            let app_context = AppContext { sender: internal_sender.clone() };
            let assistant_creator = assistant_creator_func(&app_context);
            let assistant = assistant_creator.await?;
            let strat = create_app_strategy(&assistant, FIRST_VIEW_KEY, &internal_sender).await?;
            strat.create_view_for_testing(&internal_sender)?;
            let mut app = App::new(internal_sender, strat, assistant);
            let mut frame_count = 0;
            app.app_init_common().await?;
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
                            MessageInternal::Render(_) => {
                                frame_count += 1;
                            }
                            MessageInternal::RenderAllViews => {
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

    fn render_all_views(&mut self) {
        for (_, view_controller) in &mut self.view_controllers {
            view_controller.send_update_message();
        }
    }

    /// Send a message to a specific view controller. Messages not handled by the ViewController
    /// will be forwarded to the `ViewControllerAssistant`.
    pub fn queue_message(&mut self, target: ViewKey, msg: Message) {
        self.messages.push((target, msg));
    }

    // Creates a view assistant for views that are using the render view mode feature, either
    // in Scenic or framebuffer mode.
    fn create_view_assistant(&mut self) -> Result<ViewAssistantPtr, Error> {
        Ok(self.assistant.create_view_assistant(self.next_key)?)
    }

    async fn create_view_with_params(&mut self, params: ViewStrategyParams) -> Result<(), Error> {
        let render_options = self.get_render_options();
        let view_assistant = self.create_view_assistant()?;
        let sender = &self.sender;
        let view_strat = {
            let pixel_format = self.strategy.get_pixel_format();
            let view_strat = self
                .strategy
                .create_view_strategy(self.next_key, render_options, sender.clone(), params)
                .await?;
            self.strategy.post_setup(pixel_format, sender).await?;
            view_strat
        };
        let view_controller = ViewController::new_with_strategy(
            self.next_key,
            view_assistant,
            view_strat,
            sender.clone(),
        )
        .await?;

        self.view_controllers.insert(self.next_key, view_controller);
        self.next_key += 1;
        Ok(())
    }

    fn start_services(self: &mut App) -> Result<(), Error> {
        let outgoing_services_names = self.assistant.outgoing_services_names();
        self.strategy.start_services(outgoing_services_names, self.sender.clone())?;
        Ok(())
    }

    fn get_render_options(&self) -> RenderOptions {
        self.assistant.get_render_options()
    }

    pub(crate) fn image_freed(&mut self, view_id: ViewKey, image_id: u64, collection_id: u32) {
        let view = self.get_view(view_id);
        view.image_freed(image_id, collection_id);
    }

    fn handle_vsync_parameters_changed(&mut self, phase: Time, interval: Duration) {
        for (_, view_controller) in &mut self.view_controllers {
            view_controller.handle_vsync_parameters_changed(phase, interval);
        }
    }

    fn handle_vsync_cookie(&mut self, cookie: u64) {
        if cookie != 0 {
            for (_, view_controller) in &mut self.view_controllers {
                view_controller.handle_vsync_cookie(cookie);
            }
        }
    }
}
