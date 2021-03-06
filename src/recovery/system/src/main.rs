// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::Error;
use argh::FromArgs;
use carnelian::{
    color::Color,
    drawing::{load_font, path_for_circle, DisplayRotation, FontFace, GlyphMap, Text},
    geometry::IntVector,
    input, make_message,
    render::{
        BlendMode, Composition, Context as RenderContext, Fill, FillRule, Layer, PreClear, Raster,
        RenderExt, Style,
    },
    App, AppAssistant, AppAssistantPtr, AppContext, AssistantCreatorFunc, Coord, LocalBoxFuture,
    Point, Rect, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use euclid::{
    default::{Point2D, Transform2D, Vector2D},
    point2, size2,
};
use fidl_fuchsia_input_report::ConsumerControlButton;
use fidl_fuchsia_recovery::FactoryResetMarker;
use fuchsia_async::{self as fasync, Task};
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon::{AsHandleRef, Duration, Event, Signals};
use futures::StreamExt;
use std::path::PathBuf;

const FACTORY_RESET_TIMER_IN_SECONDS: u8 = 10;
const LOGO_IMAGE_PATH: &str = "/pkg/data/logo.shed";
const BG_COLOR: Color = Color::white();
const HEADING_COLOR: Color = Color::new();
const BODY_COLOR: Color = Color { r: 0x7e, g: 0x86, b: 0x8d, a: 0xff };
const COUNTDOWN_COLOR: Color = Color { r: 0x42, g: 0x85, b: 0xf4, a: 0xff };

#[cfg(feature = "http_setup_server")]
mod setup;

#[cfg(feature = "http_setup_server")]
mod ota;

#[cfg(feature = "http_setup_server")]
use crate::setup::SetupEvent;

#[cfg(feature = "http_setup_server")]
mod storage;

mod fdr;
use fdr::{FactoryResetState, ResetEvent};

fn display_rotation_from_str(s: &str) -> Result<DisplayRotation, String> {
    match s {
        "0" => Ok(DisplayRotation::Deg0),
        "90" => Ok(DisplayRotation::Deg90),
        "180" => Ok(DisplayRotation::Deg180),
        "270" => Ok(DisplayRotation::Deg270),
        _ => Err(format!("Invalid DisplayRotation {}", s)),
    }
}

fn raster_for_circle(
    center: Point,
    radius: Coord,
    transform: Option<&Transform2D<f32>>,
    render_context: &mut RenderContext,
) -> Raster {
    let path = path_for_circle(center, radius, render_context);
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path, transform);
    raster_builder.build()
}

/// FDR
#[derive(Debug, FromArgs)]
#[argh(name = "recovery")]
struct Args {
    /// rotate
    #[argh(option, from_str_fn(display_rotation_from_str))]
    rotation: Option<DisplayRotation>,
}

enum RecoveryMessages {
    #[cfg(feature = "http_setup_server")]
    EventReceived,
    #[cfg(feature = "http_setup_server")]
    StartingOta,
    #[cfg(feature = "http_setup_server")]
    OtaFinished {
        result: Result<(), Error>,
    },
    ResetMessage(FactoryResetState),
    CountdownTick(u8),
    ResetFailed,
}

const RECOVERY_MODE_HEADLINE: &'static str = "Recovery Diagnostic Mode";
const RECOVERY_MODE_BODY: &'static str = "Press and hold both volume keys to factory reset.";

const COUNTDOWN_MODE_HEADLINE: &'static str = "Factory reset device";
const COUNTDOWN_MODE_BODY: &'static str = "Continue holding the keys to the end of the countdown. \
This will wipe all of your data from this device and reset it to factory settings.";

struct RecoveryAppAssistant {
    app_context: AppContext,
    display_rotation: DisplayRotation,
}

impl RecoveryAppAssistant {
    pub fn new(app_context: &AppContext) -> Self {
        let args: Args = argh::from_env();

        Self {
            app_context: app_context.clone(),
            display_rotation: args.rotation.unwrap_or(DisplayRotation::Deg0),
        }
    }
}

impl AppAssistant for RecoveryAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(RecoveryViewAssistant::new(
            &self.app_context,
            view_key,
            RECOVERY_MODE_HEADLINE,
            RECOVERY_MODE_BODY,
        )?))
    }

    fn get_display_rotation(&self) -> DisplayRotation {
        self.display_rotation
    }
}

fn to_raster_translation_vector(pt: Point) -> IntVector {
    pt.to_vector().to_i32()
}

struct SizedText {
    text: Text,
    #[allow(unused)]
    glyphs: GlyphMap,
}

impl SizedText {
    pub fn new(
        context: &mut RenderContext,
        label: &str,
        size: f32,
        wrap: usize,
        face: &FontFace,
    ) -> Self {
        let mut glyphs = GlyphMap::new();
        let text = Text::new(context, label, size, wrap, face, &mut glyphs);
        Self { text, glyphs }
    }
}

struct RenderResources {
    heading_label: SizedText,
    body_label: SizedText,
    countdown_label: SizedText,
    countdown_text_size: f32,
}

impl RenderResources {
    fn new(
        context: &mut RenderContext,
        min_dimension: f32,
        heading: &str,
        body: &str,
        countdown_ticks: u8,
        face: &FontFace,
    ) -> Self {
        let text_size = min_dimension / 10.0;
        let heading_label = SizedText::new(context, heading, text_size, 100, face);
        let heading_label_size = heading_label.text.bounding_box.size;

        let body_text_size = min_dimension / 18.0;
        let body_wrap = heading_label_size.width / body_text_size * 3.0;
        let body_label = SizedText::new(context, body, body_text_size, body_wrap as usize, face);

        let countdown_text_size = min_dimension / 6.0;
        let countdown_label = SizedText::new(
            context,
            &format!("{:02}", countdown_ticks),
            countdown_text_size,
            100,
            face,
        );

        Self { heading_label, body_label, countdown_label, countdown_text_size }
    }
}

struct RecoveryViewAssistant {
    face: FontFace,
    heading: String,
    body: String,
    reset_state_machine: fdr::FactoryResetStateMachine,
    app_context: AppContext,
    view_key: ViewKey,
    countdown_task: Option<Task<()>>,
    countdown_ticks: u8,
    composition: Composition,
    render_resources: Option<RenderResources>,
    rasters: Option<(Size, Vec<(Raster, Style)>, Point2D<f32>)>,
}

impl RecoveryViewAssistant {
    fn new(
        app_context: &AppContext,
        view_key: ViewKey,
        heading: &str,
        body: &str,
    ) -> Result<RecoveryViewAssistant, Error> {
        RecoveryViewAssistant::setup(app_context, view_key)?;

        let composition = Composition::new(BG_COLOR);
        let face = load_font(PathBuf::from("/pkg/data/fonts/Roboto-Regular.ttf"))?;

        Ok(RecoveryViewAssistant {
            face,
            composition,
            heading: heading.to_string(),
            body: body.to_string(),
            reset_state_machine: fdr::FactoryResetStateMachine::new(),
            app_context: app_context.clone(),
            view_key: 0,
            countdown_task: None,
            countdown_ticks: FACTORY_RESET_TIMER_IN_SECONDS,
            render_resources: None,
            rasters: None,
        })
    }

    #[cfg(not(feature = "http_setup_server"))]
    fn setup(_: &AppContext, _: ViewKey) -> Result<(), Error> {
        let f = async {
            check_blobfs_health().await;
        };
        fasync::Task::local(f).detach();
        Ok(())
    }

    #[cfg(feature = "http_setup_server")]
    fn setup(app_context: &AppContext, view_key: ViewKey) -> Result<(), Error> {
        let mut receiver = setup::start_server()?;
        let local_app_context = app_context.clone();
        let f = async move {
            while let Some(event) = receiver.next().await {
                println!("recovery: received request");
                match event {
                    SetupEvent::Root => local_app_context
                        .queue_message(view_key, make_message(RecoveryMessages::EventReceived)),
                    SetupEvent::DevhostOta { cfg } => {
                        local_app_context
                            .queue_message(view_key, make_message(RecoveryMessages::StartingOta));
                        let result = ota::run_devhost_ota(cfg).await;
                        local_app_context.queue_message(
                            view_key,
                            make_message(RecoveryMessages::OtaFinished { result }),
                        );
                    }
                }
            }
        };

        fasync::Task::local(f).detach();

        Ok(())
    }

    async fn execute_reset(view_key: ViewKey, app_context: AppContext) {
        let factory_reset_service = connect_to_service::<FactoryResetMarker>();
        let proxy = match factory_reset_service {
            Ok(marker) => marker.clone(),
            Err(error) => {
                app_context.queue_message(view_key, make_message(RecoveryMessages::ResetFailed));
                panic!("Could not connect to factory_reset_service: {}", error);
            }
        };

        println!("recovery: Executing factory reset command");

        let res = proxy.reset().await;
        match res {
            Ok(_) => {}
            Err(error) => {
                app_context.queue_message(view_key, make_message(RecoveryMessages::ResetFailed));
                eprintln!("recovery: Error occurred : {}", error);
            }
        };
    }
}

impl ViewAssistant for RecoveryViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        self.view_key = context.key;
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        // Emulate the size that Carnelian passes when the display is rotated
        let target_size = context.size;
        let min_dimension = target_size.width.min(target_size.height);

        if self.render_resources.is_none() {
            self.render_resources = Some(RenderResources::new(
                render_context,
                min_dimension,
                &self.heading,
                &self.body,
                self.countdown_ticks,
                &self.face,
            ));
        }

        let render_resources = self.render_resources.as_ref().unwrap();

        let clear_background_ext =
            RenderExt { pre_clear: Some(PreClear { color: BG_COLOR }), ..Default::default() };
        let image = render_context.get_current_image(context);

        // Cache loaded png info and position
        let (logo_size, rasters, logo_position) = self.rasters.get_or_insert_with(|| {
            match carnelian::render::Shed::open(LOGO_IMAGE_PATH) {
                Ok(shed) => {
                    let shed_size = shed.size();
                    let max_logo_size = f32::max(shed_size.width, shed_size.height);
                    let target_size_min = f32::min(target_size.width, target_size.height);
                    let scale_factor: f32 = 0.24 * target_size_min / max_logo_size;
                    let logo_size = shed_size * scale_factor;
                    let logo_position: Point2D<f32> = {
                        let x = (target_size.width - logo_size.width) / 2.0;
                        let y = target_size.height * 0.255;
                        point2(x, y)
                    };
                    let transform = Transform2D::create_scale(scale_factor, scale_factor)
                        .post_translate(Vector2D::new(logo_position.x, logo_position.y));
                    (logo_size, shed.rasters(render_context, Some(&transform)), logo_position)
                }
                Err(e) => {
                    println!("recovery: Warning: No logo found {}", e);
                    let logo_position: Point2D<f32> = {
                        let x = target_size.width / 2.0 - 72.0;
                        let y = target_size.height / 4.0;
                        point2(x, y)
                    };
                    (size2(144.0, 144.0), Vec::new(), logo_position)
                }
            }
        });

        let (heading_label_layer, heading_label_offset, heading_label_size) = {
            let heading_label_size = render_resources.heading_label.text.bounding_box.size;
            let heading_label_offset = Point::new(
                (target_size.width / 2.0) - (heading_label_size.width / 2.0),
                logo_position.y + logo_size.height,
            );

            (
                Layer {
                    raster: render_resources
                        .heading_label
                        .text
                        .raster
                        .clone()
                        .translate(to_raster_translation_vector(heading_label_offset)),
                    style: Style {
                        fill_rule: FillRule::NonZero,
                        fill: Fill::Solid(HEADING_COLOR),
                        blend_mode: BlendMode::Over,
                    },
                },
                heading_label_offset,
                heading_label_size,
            )
        };

        let body_label_layer = {
            let body_label_size = render_resources.body_label.text.bounding_box.size;
            let body_label_offset = Point::new(
                (target_size.width / 2.0) - (body_label_size.width / 2.0),
                heading_label_offset.y + heading_label_size.height * 1.5,
            );

            Layer {
                raster: render_resources
                    .body_label
                    .text
                    .raster
                    .clone()
                    .translate(to_raster_translation_vector(body_label_offset)),
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(BODY_COLOR),
                    blend_mode: BlendMode::Over,
                },
            }
        };

        if self.reset_state_machine.is_counting_down() {
            let logo_center = Rect::new(*logo_position, *logo_size).center();
            // TODO: Don't recreate this raster every frame
            let circle_raster = raster_for_circle(
                logo_center,
                logo_size.width.min(logo_size.height) / 2.0,
                None,
                render_context,
            );
            let circle_layer = Layer {
                raster: circle_raster,
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(COUNTDOWN_COLOR),
                    blend_mode: BlendMode::Over,
                },
            };

            let countdown_label_layer = {
                let countdown_label_size = render_resources.countdown_label.text.bounding_box.size;
                let countdown_label_offset = Point::new(
                    logo_center.x - countdown_label_size.width / 2.0,
                    logo_center.y - (render_resources.countdown_text_size / 2.0),
                );

                Layer {
                    raster: render_resources
                        .countdown_label
                        .text
                        .raster
                        .clone()
                        .translate(to_raster_translation_vector(countdown_label_offset)),
                    style: Style {
                        fill_rule: FillRule::NonZero,
                        fill: Fill::Solid(Color::white()),
                        blend_mode: BlendMode::Over,
                    },
                }
            };

            self.composition.replace(
                ..,
                std::iter::once(body_label_layer)
                    .chain(std::iter::once(heading_label_layer))
                    .chain(std::iter::once(countdown_label_layer))
                    .chain(std::iter::once(circle_layer)),
            );
            render_context.render(&self.composition, None, image, &clear_background_ext);
        } else {
            let raster_layer = rasters
                .iter()
                .map(|(raster, style)| Layer { raster: raster.clone(), style: *style });
            self.composition.replace(
                ..,
                raster_layer
                    .chain(std::iter::once(body_label_layer))
                    .chain(std::iter::once(heading_label_layer)),
            );
            render_context.render(&self.composition, None, image, &clear_background_ext);
        }

        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;
        Ok(())
    }

    fn handle_message(&mut self, message: carnelian::Message) {
        if let Some(message) = message.downcast_ref::<RecoveryMessages>() {
            match message {
                #[cfg(feature = "http_setup_server")]
                RecoveryMessages::EventReceived => {
                    self.body = "Got event".to_string();
                }
                #[cfg(feature = "http_setup_server")]
                RecoveryMessages::StartingOta => {
                    self.body = "Starting OTA update".to_string();
                }
                #[cfg(feature = "http_setup_server")]
                RecoveryMessages::OtaFinished { result } => {
                    if let Err(e) = result {
                        self.body = format!("OTA failed: {:?}", e);
                    } else {
                        self.body = "OTA succeeded".to_string();
                    }
                }
                RecoveryMessages::ResetMessage(state) => {
                    match state {
                        FactoryResetState::Waiting => {
                            self.heading = RECOVERY_MODE_HEADLINE.to_string();
                            self.body = RECOVERY_MODE_BODY.to_string();
                            self.render_resources = None;
                            self.app_context.request_render(self.view_key);
                        }
                        FactoryResetState::StartCountdown => {
                            let view_key = self.view_key;
                            let local_app_context = self.app_context.clone();

                            let mut counter = FACTORY_RESET_TIMER_IN_SECONDS;
                            local_app_context.queue_message(
                                view_key,
                                make_message(RecoveryMessages::CountdownTick(counter)),
                            );

                            // start the countdown timer
                            let f = async move {
                                let mut interval_timer =
                                    fasync::Interval::new(Duration::from_seconds(1));
                                while let Some(()) = interval_timer.next().await {
                                    counter -= 1;
                                    local_app_context.queue_message(
                                        view_key,
                                        make_message(RecoveryMessages::CountdownTick(counter)),
                                    );
                                    if counter == 0 {
                                        break;
                                    }
                                }
                            };
                            self.countdown_task = Some(fasync::Task::local(f));
                        }
                        FactoryResetState::CancelCountdown => {
                            self.countdown_task
                                .take()
                                .and_then(|task| Some(fasync::Task::local(task.cancel())));
                            let state = self
                                .reset_state_machine
                                .handle_event(ResetEvent::CountdownCancelled);
                            assert_eq!(state, fdr::FactoryResetState::Waiting);
                            self.app_context.queue_message(
                                self.view_key,
                                make_message(RecoveryMessages::ResetMessage(state)),
                            );
                        }
                        FactoryResetState::ExecuteReset => {
                            let view_key = self.view_key;
                            let local_app_context = self.app_context.clone();
                            let f = async move {
                                RecoveryViewAssistant::execute_reset(view_key, local_app_context)
                                    .await;
                            };
                            fasync::Task::local(f).detach();
                        }
                    };
                }
                RecoveryMessages::CountdownTick(count) => {
                    self.heading = COUNTDOWN_MODE_HEADLINE.to_string();
                    self.countdown_ticks = *count;
                    if *count == 0 {
                        self.body = "Resetting device...".to_string();
                        let state =
                            self.reset_state_machine.handle_event(ResetEvent::CountdownFinished);
                        assert_eq!(state, FactoryResetState::ExecuteReset);
                        self.app_context.queue_message(
                            self.view_key,
                            make_message(RecoveryMessages::ResetMessage(state)),
                        );
                    } else {
                        self.body = COUNTDOWN_MODE_BODY.to_string();
                    }
                    self.render_resources = None;
                    self.app_context.request_render(self.view_key);
                }
                RecoveryMessages::ResetFailed => {
                    self.heading = "Reset failed".to_string();
                    self.body = "Please restart device to try again".to_string();
                    self.render_resources = None;
                    self.app_context.request_render(self.view_key);
                }
            }
        }
    }

    fn handle_consumer_control_event(
        &mut self,
        context: &mut ViewAssistantContext,
        _: &input::Event,
        consumer_control_event: &input::consumer_control::Event,
    ) -> Result<(), Error> {
        match consumer_control_event.button {
            ConsumerControlButton::VolumeUp | ConsumerControlButton::VolumeDown => {
                let state: FactoryResetState =
                    self.reset_state_machine.handle_event(ResetEvent::ButtonPress(
                        consumer_control_event.button,
                        consumer_control_event.phase,
                    ));
                if state != fdr::FactoryResetState::ExecuteReset {
                    context.queue_message(make_message(RecoveryMessages::ResetMessage(state)));
                }
            }
            _ => {}
        }
        Ok(())
    }

    // This is to allow development of this feature on devices without consumer control buttons.
    fn handle_keyboard_event(
        &mut self,
        context: &mut ViewAssistantContext,
        event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        const HID_USAGE_KEY_F11: u32 = 0x44;
        const HID_USAGE_KEY_F12: u32 = 0x45;

        fn keyboard_to_consumer_phase(
            phase: carnelian::input::keyboard::Phase,
        ) -> carnelian::input::consumer_control::Phase {
            match phase {
                carnelian::input::keyboard::Phase::Pressed => {
                    carnelian::input::consumer_control::Phase::Down
                }
                _ => carnelian::input::consumer_control::Phase::Up,
            }
        }

        let synthetic_event = match keyboard_event.hid_usage {
            HID_USAGE_KEY_F11 => Some(input::consumer_control::Event {
                button: ConsumerControlButton::VolumeDown,
                phase: keyboard_to_consumer_phase(keyboard_event.phase),
            }),
            HID_USAGE_KEY_F12 => Some(input::consumer_control::Event {
                button: ConsumerControlButton::VolumeUp,
                phase: keyboard_to_consumer_phase(keyboard_event.phase),
            }),
            _ => None,
        };

        if let Some(synthetic_event) = synthetic_event {
            self.handle_consumer_control_event(context, event, &synthetic_event)?;
        }

        Ok(())
    }
}

fn make_app_assistant_fut(
    app_context: &AppContext,
) -> LocalBoxFuture<'_, Result<AppAssistantPtr, Error>> {
    let f = async move {
        let assistant = Box::new(RecoveryAppAssistant::new(app_context));
        Ok::<AppAssistantPtr, Error>(assistant)
    };
    Box::pin(f)
}

pub fn make_app_assistant() -> AssistantCreatorFunc {
    Box::new(make_app_assistant_fut)
}

const DEV_BLOCK: &'static str = "/dev/class/block";
use anyhow::Context as _;
use fidl::endpoints::Proxy;
use fidl_fuchsia_device::ControllerProxy;
use fs_management::{Blobfs, Filesystem};
use fuchsia_zircon::{self as zx};
use std::fs;
async fn connect_to_fdio_service(path: &str) -> Result<fidl::AsyncChannel, Error> {
    let (local, remote) = zx::Channel::create().context("Creating channel")?;
    fdio::service_connect(path, remote).context("Connecting to service")?;
    let local = fidl::AsyncChannel::from_channel(local).context("Creating AsyncChannel")?;
    Ok(local)
}
async fn get_topo_path(channel: fidl::AsyncChannel) -> Option<String> {
    let controller = ControllerProxy::from_channel(channel);
    match controller.get_topological_path().await {
        Ok(res) => match res {
            Ok(path) => {
                println!("Returned path: {}", path);
                Some(path)
            }
            Err(errno) => {
                println!("Received topo_path error value: {}", errno);
                None
            }
        },
        Err(e) => {
            println!("Error getting topological path {:#}", e);
            None
        }
    }
}
async fn check_blobfs_health() {
    println!("In diagnostics section, sleeping for 5 seconds");
    let five_seconds = std::time::Duration::from_secs(5);
    std::thread::sleep(five_seconds);
    println!("Sleep complete: running diagnostics");
    // lsblk
    match fs::read_dir(DEV_BLOCK) {
        Ok(rd) => {
            println!("Read block: OK");
            for entry in rd {
                let entry = entry.unwrap();
                let pathbuf = entry.path();
                let path = pathbuf.to_str().unwrap();
                println!("Found entry {:?} path {:?}", entry, path);

                match connect_to_fdio_service(path).await {
                    Ok(channel) => {
                        if let Some(topo_path) = get_topo_path(channel).await {
                            if topo_path.contains("/fvm/blobfs-p-1/block")
                                && !topo_path.contains("ramdisk")
                            {
                                println!("This is the expected blobfs, mount it! {}", topo_path);
                                check_blobfs(&topo_path, true);
                            } else {
                                println!("Not the fvm, skip");
                            }
                        }
                    }
                    Err(e) => {
                        println!("Error connected to service for path {}, {:#}", path, e);
                    }
                }
            }
        }
        Err(e) => {
            println!("Couldn't read block: {:#}", e);
        }
    }
}

fn check_blobfs(path: &str, readonly: bool) {
    let config = Blobfs { verbose: true, readonly: readonly, metrics: true, ..Blobfs::default() };
    let res = Filesystem::from_path(path, config).context("Filesystem::from_path");
    match res {
        Ok(b) => {
            println!("Attempting to run fsck on Blobfs");
            let mut b: Filesystem<Blobfs> = b;
            match b.fsck() {
                Ok(_) => {
                    println!("Blobfs-fsck OK");
                }
                Err(e) => println!("Error occurred during fsck() {:#}", e),
            }
            println!("Attempting to mount Blobfs");
            match b.mount("/fuchsia-blob-existing") {
                Ok(_) => {
                    println!("Mount succeeded");
                    if let Err(e) = traverse_blobfs(b, "/fuchsia-blob-existing") {
                        println!("Traverse blobfs had an unexpected error {:#}", e);
                    }
                }
                Err(e) => println!("Mount failed: {:#}", e),
            }
        }
        Err(e) => println!("Mount failed: {:#}", e),
    }
}

use crypto::digest::Digest;
use crypto::sha2::Sha256;
use std::fs::{DirEntry, File};
use std::io::Read;
fn read_entry(entry: Result<DirEntry, std::io::Error>) -> Result<(), Error> {
    println!("Check: {:?}", entry);
    let entry = entry.context("Error reading dir entry")?;
    let path_buf = entry.path();
    let path = path_buf.to_str().context("Error reading entry path")?;
    println!(" path: {}", &path);

    match fs::metadata(&path) {
        Ok(metadata) => {
            println!(" metadata: {:?}", metadata);
        }
        Err(error) => {
            println!("  Error getting file metadata {:#}", error);
        }
    };
    let mut f = File::open(path).context("Error opening file")?;
    let mut buffer = Vec::new();

    let bytes_read = f.read_to_end(&mut buffer)?;
    println!("  bytes read: {}", bytes_read);

    let mut hasher = Sha256::new();
    hasher.input(&buffer[..bytes_read]);
    let hex_val = hasher.result_str();
    println!("  Sha256: {}", hex_val);
    Ok(())
}

fn traverse_blobfs(_blobfs: Filesystem<Blobfs>, mount_path: &str) -> Result<(), Error> {
    let mut error_count = 0;
    let rd = fs::read_dir(mount_path)?;
    for entry_res in rd {
        if let Err(error) = read_entry(entry_res) {
            println!("  Unexpected error reading dir entry: {:#}", error);
            error_count += 1;
        }
    }
    if error_count == 0 {
        println!("No errors traversing blobs");
    } else {
        println!("WARNING!");
        println!("There were {} errors traversing blobs", error_count);
    }
    Ok(())
}

fn main() -> Result<(), Error> {
    println!("recovery: started");
    App::run(make_app_assistant())
}

#[cfg(test)]
mod tests {
    use super::make_app_assistant;
    use carnelian::App;

    #[test]
    fn test_ui() -> std::result::Result<(), anyhow::Error> {
        let assistant = make_app_assistant();
        App::test(assistant)
    }
}
