// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::strategies::{
        base::{create_app_strategy, AppStrategyPtr},
        framebuffer::DisplayId,
    },
    drawing::DisplayRotation,
    geometry::Size,
    input::{DeviceId, UserInputMessage},
    message::Message,
    scene::facets::FacetId,
    view::{strategies::base::ViewStrategyParams, ViewAssistantPtr, ViewController, ViewKey},
    IdGenerator2,
};
use anyhow::{bail, format_err, Context as _, Error};
use fidl_fuchsia_hardware_display::{ControllerEvent, VirtconMode};
use fidl_fuchsia_input_report as hid_input_report;
use fuchsia_async::{self as fasync, DurationExt, Timer};
use fuchsia_component::{self as component};
use fuchsia_trace::duration;
use fuchsia_zircon::{self as zx, DurationNum};
use futures::{
    channel::mpsc::{unbounded, UnboundedSender},
    future::{Either, Future},
    StreamExt,
};
use once_cell::sync::OnceCell;
use serde::Deserialize;
use std::{any::Any, collections::BTreeMap, fmt::Debug, fs, path::PathBuf, pin::Pin};
use toml;

pub(crate) mod strategies;

/// Type alias for a non-sync future
pub type LocalBoxFuture<'a, T> = Pin<Box<dyn Future<Output = T> + 'a>>;

fn virtcon_mode_from_str(mode_str: &str) -> Result<Option<VirtconMode>, Error> {
    match mode_str {
        "forced" => Ok(Some(VirtconMode::Forced)),
        "fallback" => Ok(Some(VirtconMode::Fallback)),
        _ => Err(format_err!("Invalid VirtconMode {}", mode_str)),
    }
}

fn deserialize_virtcon_mode<'de, D>(deserializer: D) -> Result<Option<VirtconMode>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let str = String::deserialize(deserializer)?;
    virtcon_mode_from_str(&str).map_err(serde::de::Error::custom)
}

const fn keyboard_autorepeat_default() -> bool {
    true
}

fn duration_from_millis(time_in_millis: u64) -> Result<std::time::Duration, Error> {
    Ok(std::time::Duration::from_millis(time_in_millis))
}

fn deserialize_millis<'de, D>(deserializer: D) -> Result<std::time::Duration, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let ms = u64::deserialize(deserializer)?;
    duration_from_millis(ms).map_err(serde::de::Error::custom)
}

const fn keyboard_autorepeat_slow_interval_default() -> std::time::Duration {
    const KEYBOARD_AUTOREPEAT_SLOW_INTERVAL: std::time::Duration =
        std::time::Duration::from_millis(250);
    KEYBOARD_AUTOREPEAT_SLOW_INTERVAL
}

const fn keyboard_autorepeat_fast_interval_default() -> std::time::Duration {
    const KEYBOARD_AUTOREPEAT_FAST_INTERVAL: std::time::Duration =
        std::time::Duration::from_millis(50);
    KEYBOARD_AUTOREPEAT_FAST_INTERVAL
}

const fn display_resource_release_delay_default() -> std::time::Duration {
    const DISPLAY_RESOURCE_RELEASE_DELAY_DEFAULT: std::time::Duration =
        std::time::Duration::from_secs(5);
    DISPLAY_RESOURCE_RELEASE_DELAY_DEFAULT
}

const fn startup_delay_default() -> std::time::Duration {
    const STARTUP_DELAY_DEFAULT: std::time::Duration = std::time::Duration::from_secs(0);
    STARTUP_DELAY_DEFAULT
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ViewMode {
    Auto,
    Hosted,
    Direct,
}

impl Default for ViewMode {
    fn default() -> Self {
        Self::Auto
    }
}

#[derive(Debug, Deserialize)]
pub struct Config {
    #[serde(default = "keyboard_autorepeat_default")]
    /// Whether, when running without Scenic, this application should
    /// receive keyboard repeat events.
    pub keyboard_autorepeat: bool,
    #[serde(
        default = "keyboard_autorepeat_slow_interval_default",
        deserialize_with = "deserialize_millis"
    )]
    /// The initial and maximum interval between keyboard repeat events, in
    /// milliseconds, when running without Scenic.
    pub keyboard_autorepeat_slow_interval: std::time::Duration,
    #[serde(
        default = "keyboard_autorepeat_fast_interval_default",
        deserialize_with = "deserialize_millis"
    )]
    /// The minimum interval between keyboard repeat events, in
    /// milliseconds, when running without Scenic.
    pub keyboard_autorepeat_fast_interval: std::time::Duration,
    #[serde(default)]
    /// Whether to try to use hardware rendering (Spinel).
    pub use_spinel: bool,
    /// What mode to use when acting as a virtual console.
    #[serde(default, deserialize_with = "deserialize_virtcon_mode")]
    pub virtcon_mode: Option<VirtconMode>,
    /// What sort of view system to use.
    #[serde(default)]
    pub view_mode: ViewMode,
    #[serde(default)]
    /// Application option to exercise transparent rotation.
    pub display_rotation: DisplayRotation,
    #[serde(default)]
    /// Application option to select keymap. If named keymap is not found
    /// the fallback is US QWERTY.
    pub keymap_name: Option<String>,
    #[serde(
        default = "display_resource_release_delay_default",
        deserialize_with = "deserialize_millis"
    )]
    /// How long should carnelian wait before releasing display resources when
    /// it loses ownership of the display while running directly on the display. The default
    /// value is five seconds, so that the resource will not be rapidly allocated
    /// and deallocated when switching quickly between virtcon and the regular display.
    pub display_resource_release_delay: std::time::Duration,
    #[serde(default)]
    /// In a bringup build the display controller might not support multiple
    /// buffers so Carnelian might have to run with only a
    /// single buffer. This configuration option is to allow testing rendering
    /// with a single buffer even in build that supports multiple.
    pub buffer_count: Option<usize>,
    #[serde(default)]
    /// Whether input events are needed.
    pub input: bool,
    #[serde(default)]
    /// Whether output can be translucent and needs blending.
    pub needs_blending: bool,
    #[serde(default = "startup_delay_default", deserialize_with = "deserialize_millis")]
    /// How long to wait before entering event loop.
    pub startup_delay: std::time::Duration,
}

impl Config {
    pub(crate) fn get() -> &'static Config {
        // Some input tests access the config. Rather than requiring setup everywhere,
        // default the config values for testing purposes.
        CONFIG.get_or_init(|| Config::default())
    }
}

impl Default for Config {
    fn default() -> Self {
        Self {
            keyboard_autorepeat: keyboard_autorepeat_default(),
            keyboard_autorepeat_slow_interval: keyboard_autorepeat_slow_interval_default(),
            keyboard_autorepeat_fast_interval: keyboard_autorepeat_fast_interval_default(),
            use_spinel: false,
            virtcon_mode: None,
            view_mode: ViewMode::default(),
            display_rotation: DisplayRotation::Deg0,
            keymap_name: None,
            display_resource_release_delay: display_resource_release_delay_default(),
            buffer_count: None,
            input: true,
            needs_blending: false,
            startup_delay: Default::default(),
        }
    }
}

pub(crate) static CONFIG: OnceCell<Config> = OnceCell::new();

pub(crate) type InternalSender = UnboundedSender<MessageInternal>;

#[derive(Debug, Clone, Copy)]
pub enum MessageTarget {
    Facet(ViewKey, FacetId),
    View(ViewKey),
    Application,
}

pub type CreateViewOptions = Box<dyn Any>;

/// Context struct passed to the application assistant creator
// function.
#[derive(Clone)]
pub struct AppSender {
    sender: InternalSender,
}

impl AppSender {
    /// Send a message to a view controller.
    pub fn queue_message(&self, target: MessageTarget, message: Message) {
        self.sender
            .unbounded_send(MessageInternal::TargetedMessage(target, message))
            .expect("AppSender::queue_message - unbounded_send");
    }

    /// Request that a frame be rendered at the next appropriate time.
    pub fn request_render(&self, target: ViewKey) {
        self.sender
            .unbounded_send(MessageInternal::RequestRender(target))
            .expect("AppSender::request_render - unbounded_send");
    }

    pub fn set_virtcon_mode(&self, virtcon_mode: VirtconMode) {
        self.sender
            .unbounded_send(MessageInternal::SetVirtconMode(virtcon_mode))
            .expect("AppSender::set_virtcon_mode - unbounded_send");
    }

    pub fn create_additional_view(&self, options: Option<CreateViewOptions>) -> ViewKey {
        let view_key = IdGenerator2::<ViewKey>::next().expect("view_key");
        self.sender
            .unbounded_send(MessageInternal::CreateAdditionalView(view_key, options))
            .expect("AppSender::create_additional_view - unbounded_send");
        view_key
    }

    pub fn close_additional_view(&self, view_key: ViewKey) {
        self.sender
            .unbounded_send(MessageInternal::CloseAdditionalView(view_key))
            .expect("AppSender::close_additional_view - unbounded_send");
    }

    /// Create an futures mpsc sender and a task to poll the receiver and
    /// forward the message to the app sender. This setup works around the problem
    /// that dyn Any references cannot be `Send` at the cost of an extra trip through
    /// the executor.
    /// The 'static trait bounds here means that messages send across thread may not
    /// contain any non-static references. The data in the messages must be owned, but
    /// no not themselves need to be static.
    pub fn create_cross_thread_sender<T: 'static + Send>(
        &self,
        target: MessageTarget,
    ) -> UnboundedSender<T> {
        let (sender, mut receiver) = unbounded::<T>();
        let app_sender = self.sender.clone();
        let f = async move {
            while let Some(message) = receiver.next().await {
                app_sender
                    .unbounded_send(MessageInternal::TargetedMessage(target, Box::new(message)))
                    .expect("unbounded_send");
            }
        };
        // This task can be detached as it will exit when the unbounded sender
        // it provides is dropped.
        fasync::Task::local(f).detach();
        sender
    }

    /// Set the display's gamma table. Used only for factory diagnostics.
    pub fn import_and_set_gamma_table(
        &self,
        display_id: u64,
        gamma_table_id: u64,
        r: GammaValues,
        g: GammaValues,
        b: GammaValues,
    ) {
        self.sender
            .unbounded_send(MessageInternal::ImportAndSetGamaTable(
                display_id,
                gamma_table_id,
                Box::new(r),
                Box::new(g),
                Box::new(b),
            ))
            .expect("AppSender::import_gamma_table - unbounded_send");
    }

    /// Create an context for testing things that need an app context.
    pub fn new_for_testing_purposes_only() -> AppSender {
        let (internal_sender, _) = unbounded::<MessageInternal>();
        AppSender { sender: internal_sender }
    }
}

fn make_app_assistant_fut<T: AppAssistant + Default + 'static>(
    _: &AppSender,
) -> LocalBoxFuture<'_, Result<AppAssistantPtr, Error>> {
    let f = async move {
        let assistant = Box::new(T::default());
        Ok::<AppAssistantPtr, Error>(assistant)
    };
    Box::pin(f)
}

/// Convenience function to create an application assistant that implements Default.
pub fn make_app_assistant<T: AppAssistant + Default + 'static>() -> AssistantCreatorFunc {
    Box::new(make_app_assistant_fut::<T>)
}

/// Parameter struction for view creation
pub struct ViewCreationParameters {
    pub view_key: ViewKey,
    pub app_sender: AppSender,
    pub display_id: Option<u64>,
    pub options: Option<Box<dyn Any>>,
}

impl Debug for ViewCreationParameters {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("ViewCreationParameters")
            .field("view_key", &self.view_key)
            .field("display_id", &self.display_id)
            .field("options", &self.options)
            .finish()
    }
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
    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        todo!("Must implement create_view_assistant_with_parameters or create_view_assistant");
    }

    /// Called when the Fuchsia view system requests that a view be created. Provides
    /// parameters to view creation that include anything provided the the view creation
    /// requestor and an AppSender.
    fn create_view_assistant_with_parameters(
        &mut self,
        params: ViewCreationParameters,
    ) -> Result<ViewAssistantPtr, Error> {
        self.create_view_assistant(params.view_key)
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
        return Err(format_err!("handle_service_connection_request not implemented"));
    }

    /// Filter Carnelian configuration at runtime, if needed.
    fn filter_config(&mut self, _config: &mut Config) {}

    /// This method is called when `App::queue_message` is called with `Application`
    /// as target.
    #[allow(unused_variables)]
    fn handle_message(&mut self, message: Message) {}
}

/// Reference to an application assistant.
pub type AppAssistantPtr = Box<dyn AppAssistant>;

/// Struct that implements module-wide responsibilities, currently limited
/// to creating views on request.
pub struct App {
    strategy: AppStrategyPtr,
    view_controllers: BTreeMap<ViewKey, ViewController>,
    assistant: AppAssistantPtr,
    messages: Vec<(ViewKey, Message)>,
    sender: InternalSender,
}

pub type GammaValues = [f32; 256];
type BoxedGammaValues = Box<[f32; 256]>;

#[derive(Debug)]
pub(crate) enum MessageInternal {
    ServiceConnection(zx::Channel, &'static str),
    CreateView(ViewStrategyParams),
    CreateAdditionalView(ViewKey, Option<CreateViewOptions>),
    CloseAdditionalView(ViewKey),
    MetricsChanged(ViewKey, Size),
    SizeChanged(ViewKey, Size),
    ScenicPresentSubmitted(ViewKey, fidl_fuchsia_scenic_scheduling::FuturePresentationTimes),
    ScenicPresentDone(ViewKey, fidl_fuchsia_scenic_scheduling::FramePresentedInfo),
    Focus(ViewKey, bool),
    CloseViewsOnDisplay(DisplayId),
    RequestRender(ViewKey),
    Render(ViewKey),
    ImageFreed(ViewKey, u64, u32),
    TargetedMessage(MessageTarget, Message),
    RegisterDevice(DeviceId, hid_input_report::DeviceDescriptor),
    InputReport(DeviceId, hid_input_report::InputReport),
    KeyboardAutoRepeat(DeviceId),
    OwnershipChanged(bool),
    DropDisplayResources,
    FlatlandOnNextFrameBegin(ViewKey, fidl_fuchsia_ui_composition::OnNextFrameBeginValues),
    FlatlandOnFramePresented(ViewKey, fidl_fuchsia_scenic_scheduling::FramePresentedInfo),
    FlatlandOnError(ViewKey, fuchsia_scenic::flatland::FlatlandError),
    NewDisplayController(PathBuf),
    DisplayControllerEvent(ControllerEvent),
    SetVirtconMode(VirtconMode),
    ImportAndSetGamaTable(u64, u64, BoxedGammaValues, BoxedGammaValues, BoxedGammaValues),
    UserInputMessage(ViewKey, UserInputMessage),
}

/// Future that returns an application assistant.
pub type AssistantCreator<'a> = LocalBoxFuture<'a, Result<AppAssistantPtr, Error>>;
/// Function that creates an AssistantCreator future.
pub type AssistantCreatorFunc = Box<dyn FnOnce(&AppSender) -> AssistantCreator<'_>>;

impl App {
    fn new(sender: InternalSender, strategy: AppStrategyPtr, assistant: AppAssistantPtr) -> App {
        App { strategy, view_controllers: BTreeMap::new(), assistant, messages: Vec::new(), sender }
    }

    fn load_and_filter_config(assistant: &mut AppAssistantPtr) -> Result<(), Error> {
        let mut config = Self::load_config()?;
        assistant.filter_config(&mut config);
        CONFIG.set(config).expect("config set");
        Ok(())
    }

    /// Starts an application based on Carnelian. The `assistant` parameter will
    /// be used to create new views when asked to do so by the Fuchsia view system.
    pub fn run(assistant_creator_func: AssistantCreatorFunc) -> Result<(), Error> {
        let mut executor = fasync::LocalExecutor::new().context("Error creating executor")?;
        let (internal_sender, mut internal_receiver) = unbounded::<MessageInternal>();
        let f = async {
            let app_sender = AppSender { sender: internal_sender.clone() };
            let assistant_creator = assistant_creator_func(&app_sender);
            let mut assistant = assistant_creator.await?;
            Self::load_and_filter_config(&mut assistant)?;
            let strat = create_app_strategy(&internal_sender).await?;
            let mut app = App::new(internal_sender, strat, assistant);
            app.app_init_common().await?;
            let startup_delay = Config::get().startup_delay;
            if !startup_delay.is_zero() {
                duration!("gfx", "App::run-startup-delay");
                std::thread::sleep(Config::get().startup_delay);
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
                            .handle_service_connection_request(service_name, channel)
                            .unwrap_or_else(|e| {
                                eprintln!("error running {} server: {:?}", service_name, e)
                            });
                    }
                    Err(e) => eprintln!("error asyncifying channel: {:?}", e),
                }
            }
            MessageInternal::CreateView(params) => {
                self.create_view_with_params(params, None).await?
            }
            MessageInternal::CreateAdditionalView(view_key, options) => {
                self.create_additional_view(view_key, options).await?
            }
            MessageInternal::CloseAdditionalView(view_key) => {
                self.close_additional_view(view_key)?;
            }
            MessageInternal::MetricsChanged(view_id, metrics) => {
                if let Ok(view) = self.get_view(view_id) {
                    view.handle_metrics_changed(metrics);
                }
            }
            MessageInternal::SizeChanged(view_id, new_size) => {
                if let Ok(view) = self.get_view(view_id) {
                    view.handle_size_changed(new_size);
                }
            }
            MessageInternal::ScenicPresentSubmitted(view_id, info) => {
                if let Ok(view) = self.get_view(view_id) {
                    view.present_submitted(info);
                }
            }
            MessageInternal::ScenicPresentDone(view_id, info) => {
                if let Ok(view) = self.get_view(view_id) {
                    view.present_done(info);
                }
            }
            MessageInternal::Focus(view_id, focused) => {
                if let Ok(view) = self.get_view(view_id) {
                    view.focus(focused);
                }
            }
            MessageInternal::RequestRender(view_id) => {
                if let Ok(view) = self.get_view(view_id) {
                    view.request_render();
                }
            }
            MessageInternal::Render(view_id) => {
                if let Ok(view) = self.get_view(view_id) {
                    view.render().await;
                }
            }
            MessageInternal::CloseViewsOnDisplay(display_id) => {
                let view_keys = self.get_view_keys_for_display(display_id);
                for view_key in view_keys.into_iter() {
                    self.close_view(view_key);
                }
            }
            MessageInternal::ImageFreed(view_id, image_id, collection_id) => {
                self.image_freed(view_id, image_id, collection_id)
            }
            MessageInternal::TargetedMessage(target, message) => match target {
                MessageTarget::Facet(view_id, facet_id) => {
                    let view = self.get_view(view_id).context("TargetedMessage")?;
                    view.send_facet_message(facet_id, message).context("TargetedMessage")?;
                }
                MessageTarget::View(view_id) => {
                    let view = self.get_view(view_id).context("TargetedMessage")?;
                    view.send_message(message);
                }
                MessageTarget::Application => {
                    self.assistant.handle_message(message);
                }
            },
            MessageInternal::RegisterDevice(device_id, device_descriptor) => {
                self.strategy.handle_register_input_device(&device_id, &device_descriptor);
            }
            MessageInternal::InputReport(device_id, input_report) => {
                let input_events = self.strategy.handle_input_report(&device_id, &input_report);
                if let Some(focused_view_key) = self.get_focused_view_key() {
                    let view = self.get_view(focused_view_key).context("InputReport")?;
                    view.handle_input_events(input_events).context("InputReport")?;
                } else {
                    eprintln!("dropping input report due to no focused view");
                }
            }
            MessageInternal::KeyboardAutoRepeat(device_id) => {
                let input_events = self.strategy.handle_keyboard_autorepeat(&device_id);
                if let Some(focused_view_key) = self.get_focused_view_key() {
                    let view = self.get_view(focused_view_key).context("KeyboardAutoRepeat")?;
                    view.handle_input_events(input_events).context("KeyboardAutoRepeat")?;
                } else {
                    eprintln!("dropping keyboard auto repeat due to no focused view");
                }
            }
            MessageInternal::UserInputMessage(view_id, user_input_message) => {
                let view = self.get_view(view_id).context("UserInputMessage")?;
                view.handle_user_input_message(user_input_message)?;
            }
            MessageInternal::OwnershipChanged(owned) => {
                self.ownership_changed(owned);
            }
            MessageInternal::DropDisplayResources => {
                self.drop_display_resources();
            }
            MessageInternal::FlatlandOnNextFrameBegin(view_id, info) => {
                let view = self.get_view(view_id).context("FlatlandOnNextFrameBegin")?;
                view.handle_on_next_frame_begin(&info);
            }
            MessageInternal::FlatlandOnFramePresented(view_id, info) => {
                let view = self.get_view(view_id).context("FlatlandOnFramePresented")?;
                view.present_done(info);
            }
            MessageInternal::FlatlandOnError(view_id, error) => {
                eprintln!("flatland error view: {}, error: {:#?}", view_id, error);
            }
            MessageInternal::NewDisplayController(display_path) => {
                self.strategy.handle_new_display_controller(display_path).await;
            }
            MessageInternal::DisplayControllerEvent(event) => match event {
                ControllerEvent::OnVsync { display_id, .. } => {
                    if let Some(view_key) =
                        self.strategy.get_visible_view_key_for_display(display_id)
                    {
                        if let Ok(view) = self.get_view(view_key) {
                            view.handle_display_controller_event(event).await;
                        } else {
                            // We seem to get two vsyncs after the display is removed.
                            // Log it to help run down why that is.
                            eprintln!("vsync for display {} with no view", display_id);
                        }
                    }
                }

                _ => self.strategy.handle_display_controller_event(event).await,
            },
            MessageInternal::SetVirtconMode(virtcon_mode) => {
                self.strategy.set_virtcon_mode(virtcon_mode);
            }
            MessageInternal::ImportAndSetGamaTable(display_id, gamma_table_id, r, g, b) => {
                self.strategy.import_and_set_gamma_table(display_id, gamma_table_id, r, g, b);
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
        let mut executor = fasync::LocalExecutor::new().context("Error creating executor")?;
        let (internal_sender, mut internal_receiver) = unbounded::<MessageInternal>();
        let f = async {
            let app_sender = AppSender { sender: internal_sender.clone() };
            let assistant_creator = assistant_creator_func(&app_sender);
            let mut assistant = assistant_creator.await?;
            Self::load_and_filter_config(&mut assistant)?;
            let strat = create_app_strategy(&internal_sender).await?;
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

    fn get_focused_view_key(&self) -> Option<ViewKey> {
        self.strategy.get_focused_view_key()
    }

    fn get_view(&mut self, view_key: ViewKey) -> Result<&mut ViewController, Error> {
        if let Some(view) = self.view_controllers.get_mut(&view_key) {
            Ok(view)
        } else {
            bail!("Could not find view controller for {}", view_key);
        }
    }

    fn get_view_keys_for_display(&mut self, display_id: u64) -> Vec<ViewKey> {
        self.view_controllers
            .iter()
            .filter_map(|(view_key, view_controller)| {
                view_controller.is_hosted_on_display(display_id).then(|| *view_key)
            })
            .collect()
    }

    fn close_view(&mut self, view_key: ViewKey) {
        let view = self.view_controllers.remove(&view_key);
        if let Some(mut view) = view {
            view.close();
        }
        self.strategy.handle_view_closed(view_key);
    }

    fn ownership_changed(&mut self, owned: bool) {
        for (_, view_controller) in &mut self.view_controllers {
            view_controller.ownership_changed(owned);
        }
    }

    fn drop_display_resources(&mut self) {
        for (_, view_controller) in &mut self.view_controllers {
            view_controller.drop_display_resources();
        }
    }

    /// Send a message to a specific view controller. Messages not handled by the ViewController
    /// will be forwarded to the `ViewControllerAssistant`.
    pub fn queue_message(&mut self, target: ViewKey, msg: Message) {
        self.messages.push((target, msg));
    }

    // Creates a view assistant for views that are using the render view mode feature, either
    // in hosted or direct mode.
    fn create_view_assistant(
        &mut self,
        view_key: ViewKey,
        display_id: Option<u64>,
        options: Option<CreateViewOptions>,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(self.assistant.create_view_assistant_with_parameters(ViewCreationParameters {
            view_key,
            display_id,
            app_sender: AppSender { sender: self.sender.clone() },
            options,
        })?)
    }

    async fn create_view_with_params(
        &mut self,
        params: ViewStrategyParams,
        options: Option<CreateViewOptions>,
    ) -> Result<(), Error> {
        let view_key = if let Some(view_key) = params.view_key() {
            view_key
        } else {
            IdGenerator2::<ViewKey>::next().expect("view_key")
        };
        let view_assistant = self
            .create_view_assistant(view_key, params.display_id(), options)
            .context("create_view_assistant")?;
        let sender = &self.sender;
        let view_strat = {
            let view_strat = self
                .strategy
                .create_view_strategy(view_key, sender.clone(), params)
                .await
                .context("create_view_strategy")?;
            self.strategy.post_setup(sender).await.context("post_setup")?;
            view_strat
        };
        let view_controller =
            ViewController::new_with_strategy(view_key, view_assistant, view_strat, sender.clone())
                .await
                .context("new_with_strategy")?;

        self.view_controllers.insert(view_key, view_controller);
        Ok(())
    }

    async fn create_additional_view(
        &mut self,
        view_key: ViewKey,
        options: Option<CreateViewOptions>,
    ) -> Result<(), Error> {
        let params = self.strategy.create_view_strategy_params_for_additional_view(view_key);
        self.create_view_with_params(params, options).await
    }

    fn close_additional_view(&mut self, view_key: ViewKey) -> Result<(), Error> {
        self.close_view(view_key);
        Ok(())
    }

    fn start_services(self: &mut App) -> Result<(), Error> {
        let mut fs = component::server::ServiceFs::new_local();

        inspect_runtime::serve(fuchsia_inspect::component::inspector(), &mut fs)
            .unwrap_or_else(|e| println!("Unable to start inspect support: {}", e));

        self.strategy.start_services(self.sender.clone(), &mut fs)?;

        let outgoing_services_names = self.assistant.outgoing_services_names();
        let mut public = fs.dir("svc");
        for name in outgoing_services_names {
            let sender = self.sender.clone();
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

        fasync::Task::local(fs.collect()).detach();
        Ok(())
    }

    pub(crate) fn image_freed(&mut self, view_id: ViewKey, image_id: u64, collection_id: u32) {
        if let Ok(view) = self.get_view(view_id) {
            view.image_freed(image_id, collection_id);
        }
    }

    fn load_config() -> Result<Config, Error> {
        const CARNELIAN_CONFIG_PATH: &str = "/pkg/data/config/carnelian.toml";
        let config_path = PathBuf::from(CARNELIAN_CONFIG_PATH);
        if !config_path.exists() {
            return Ok(Config::default());
        }
        let config_contents = fs::read_to_string(config_path)?;
        let config = toml::from_str(&config_contents)?;
        Ok(config)
    }
}
