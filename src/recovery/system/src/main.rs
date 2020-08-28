// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;
use carnelian::{
    color::Color,
    drawing::{path_for_circle, DisplayRotation, FontFace, GlyphMap, Text},
    geometry::IntVector,
    input, make_message,
    render::{
        BlendMode, Composition, Context as RenderContext, CopyRegion, Fill, FillRule, Image, Layer,
        PostCopy, PreClear, Raster, RenderExt, Style,
    },
    App, AppAssistant, AppAssistantPtr, AppContext, AssistantCreatorFunc, Coord, LocalBoxFuture,
    Point, Rect, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use euclid::{
    default::{Point2D, Transform2D, Vector2D},
    point2,
};
use fidl_fuchsia_input_report::ConsumerControlButton;
use fidl_fuchsia_recovery::FactoryResetMarker;
use fuchsia_async::{self as fasync, Task};
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon::{AsHandleRef, Duration, Event, Signals};
use futures::StreamExt;
use std::fs::File;

const FACTORY_RESET_TIMER_IN_SECONDS: u8 = 10;
const LOGO_IMAGE_PATH: &str = "/pkg/data/logo.png";
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

static FONT_DATA: &'static [u8] =
    include_bytes!("../../../../prebuilt/third_party/fonts/roboto/Roboto-Regular.ttf");

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

struct PngImage {
    file: String,
    loaded_info: Option<(Size, Image, Point2D<f32>)>,
}

const RECOVERY_MODE_HEADLINE: &'static str = "Recovery Mode";
const RECOVERY_MODE_BODY: &'static str = "Press and hold both volume keys to factory reset.";

const COUNTDOWN_MODE_HEADLINE: &'static str = "Factory reset device";
const COUNTDOWN_MODE_BODY: &'static str = "Continue holding the keys to the end of the countdown. \
This will wipe all of your data from this device and reset it to factory settings.";

struct RecoveryAppAssistant {
    app_context: AppContext,
}

impl RecoveryAppAssistant {
    pub fn new(app_context: &AppContext) -> Self {
        Self { app_context: app_context.clone() }
    }
}

impl AppAssistant for RecoveryAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let args: Args = argh::from_env();
        Ok(Box::new(RecoveryViewAssistant::new(
            &self.app_context,
            args.rotation.unwrap_or(DisplayRotation::Deg0),
            view_key,
            RECOVERY_MODE_HEADLINE,
            RECOVERY_MODE_BODY,
        )?))
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
        display_rotation: DisplayRotation,
        label: &str,
        size: f32,
        wrap: usize,
        face: &FontFace<'_>,
    ) -> Self {
        let mut glyphs = GlyphMap::new_with_rotation(display_rotation);
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
        display_rotation: DisplayRotation,
        min_dimension: f32,
        heading: &str,
        body: &str,
        countdown_ticks: u8,
        face: &FontFace<'_>,
    ) -> Self {
        let text_size = min_dimension / 10.0;
        let heading_label =
            SizedText::new(context, display_rotation, heading, text_size, 100, face);
        let heading_label_size = heading_label.text.bounding_box.size;

        let body_text_size = min_dimension / 18.0;
        let body_wrap = heading_label_size.width / body_text_size * 3.0;
        let body_label = SizedText::new(
            context,
            display_rotation,
            body,
            body_text_size,
            body_wrap as usize,
            face,
        );

        let countdown_text_size = min_dimension / 4.0;
        let countdown_label = SizedText::new(
            context,
            display_rotation,
            &format!("{:02}", countdown_ticks),
            countdown_text_size,
            100,
            face,
        );

        Self { heading_label, body_label, countdown_label, countdown_text_size }
    }
}

struct RecoveryViewAssistant<'a> {
    display_rotation: DisplayRotation,
    face: FontFace<'a>,
    heading: String,
    body: String,
    reset_state_machine: fdr::FactoryResetStateMachine,
    app_context: AppContext,
    view_key: ViewKey,
    countdown_task: Option<Task<()>>,
    countdown_ticks: u8,
    composition: Composition,
    render_resources: Option<RenderResources>,
    logo_image: PngImage,
}

impl<'a> RecoveryViewAssistant<'a> {
    fn new(
        app_context: &AppContext,
        display_rotation: DisplayRotation,
        view_key: ViewKey,
        heading: &str,
        body: &str,
    ) -> Result<RecoveryViewAssistant<'a>, Error> {
        RecoveryViewAssistant::setup(app_context, view_key)?;

        let composition = Composition::new(BG_COLOR);
        let face = FontFace::new(FONT_DATA)?;
        let logo_image = PngImage { file: LOGO_IMAGE_PATH.to_string(), loaded_info: None };

        Ok(RecoveryViewAssistant {
            display_rotation,
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
            logo_image,
        })
    }

    #[cfg(not(feature = "http_setup_server"))]
    fn setup(_: &AppContext, _: ViewKey) -> Result<(), Error> {
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

    fn target_size(&self, size: Size) -> Size {
        match self.display_rotation {
            DisplayRotation::Deg90 | DisplayRotation::Deg270 => Size::new(size.height, size.width),
            DisplayRotation::Deg0 | DisplayRotation::Deg180 => size,
        }
    }
}

impl ViewAssistant for RecoveryViewAssistant<'_> {
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
        let target_size = self.target_size(context.size);
        let min_dimension = target_size.width.min(target_size.height);

        if self.render_resources.is_none() {
            self.render_resources = Some(RenderResources::new(
                render_context,
                self.display_rotation,
                min_dimension,
                &self.heading,
                &self.body,
                self.countdown_ticks,
                &self.face,
            ));
        }

        let render_resources = self.render_resources.as_ref().unwrap();

        // Create a presentation to display tranformation
        let transform =
            self.display_rotation.transform(&target_size).unwrap_or(Transform2D::identity());

        let clear_background_ext =
            RenderExt { pre_clear: Some(PreClear { color: BG_COLOR }), ..Default::default() };
        let image = render_context.get_current_image(context);

        let (logo_size, png_image, logo_position) =
            self.logo_image.loaded_info.take().unwrap_or_else(|| {
                let file = File::open(&self.logo_image.file).expect("failed to load logo png");
                let decoder = png::Decoder::new(file);
                let (info, mut reader) = decoder.read_info().unwrap();
                let image = render_context
                    .new_image_from_png(&mut reader)
                    .expect(&format!("failed to decode file {}", &self.logo_image.file));
                let size = Size::new(info.width as f32, info.height as f32);

                // Calculate position for centering the logo image
                let logo_position = {
                    let x = (target_size.width - size.width) / 2.0;
                    let y = target_size.height / 2.0 - size.height;
                    point2(x, y)
                };

                (size, image, logo_position)
            });

        // Cache loaded png info and position
        self.logo_image.loaded_info.replace((logo_size, png_image, logo_position));

        let (heading_label_layer, heading_label_offset, heading_label_size) = {
            let heading_label_size = render_resources.heading_label.text.bounding_box.size;
            let heading_label_offset = Point::new(
                (target_size.width / 2.0) - (heading_label_size.width / 2.0),
                logo_position.y + logo_size.height,
            );

            let display_heading_label_offset = transform.transform_point(heading_label_offset);

            (
                Layer {
                    raster: render_resources
                        .heading_label
                        .text
                        .raster
                        .clone()
                        .translate(to_raster_translation_vector(display_heading_label_offset)),
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
            let display_body_label_offset = transform.transform_point(body_label_offset);

            Layer {
                raster: render_resources
                    .body_label
                    .text
                    .raster
                    .clone()
                    .translate(to_raster_translation_vector(display_body_label_offset)),
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(BODY_COLOR),
                    blend_mode: BlendMode::Over,
                },
            }
        };

        if self.reset_state_machine.is_counting_down() {
            let logo_center = Rect::new(logo_position, logo_size).center();
            // TODO: Don't recreate this raster every frame
            let circle_raster = raster_for_circle(
                logo_center,
                logo_size.width.min(logo_size.height) / 2.0,
                Some(&transform.to_untyped()),
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

            let countdown_label_layer =
                {
                    let countdown_label_size =
                        render_resources.countdown_label.text.bounding_box.size;
                    let countdown_label_offset = Point::new(
                        logo_center.x - countdown_label_size.width / 2.0,
                        logo_center.y - (render_resources.countdown_text_size / 2.0),
                    );
                    let display_countdown_label_offset =
                        transform.transform_point(countdown_label_offset);

                    Layer {
                        raster: render_resources.countdown_label.text.raster.clone().translate(
                            to_raster_translation_vector(display_countdown_label_offset),
                        ),
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
            // Determine visible rect and copy |png_image| to |image|.
            let dst_rect =
                transform.transform_rect(&Rect::new(logo_position, logo_size)).to_untyped();
            let output_rect =
                transform.transform_rect(&Rect::new(Point2D::zero(), target_size)).to_untyped();
            let png_ext = RenderExt {
                post_copy: dst_rect.intersection(&output_rect).map(|visible_rect| PostCopy {
                    image,
                    color: BG_COLOR,
                    exposure_distance: Vector2D::zero(),
                    copy_region: CopyRegion {
                        src_offset: (visible_rect.origin - dst_rect.origin).to_point().to_u32(),
                        dst_offset: visible_rect.origin.to_u32(),
                        extent: visible_rect.size.to_u32(),
                    },
                }),
                ..Default::default()
            };
            self.composition.replace(
                ..,
                std::iter::once(body_label_layer).chain(std::iter::once(heading_label_layer)),
            );
            render_context.render(&self.composition, None, image, &clear_background_ext);
            render_context.render(&self.composition, None, png_image, &png_ext);
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
