// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Error, Result};
#[cfg(feature = "debug_touch_to_update")]
use carnelian::input;
use carnelian::{
    app::Config,
    color::Color,
    drawing::DisplayRotation,
    make_message,
    render::{rive::load_rive, Context as RenderContext},
    scene::{
        facets::{RiveFacet, TextHorizontalAlignment},
        layout::{CrossAxisAlignment, MainAxisAlignment},
        scene::{Scene, SceneBuilder},
    },
    App, AppAssistant, AppAssistantPtr, AppSender, AssistantCreatorFunc, LocalBoxFuture, Message,
    MessageTarget, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use euclid::size2;
use fidl::endpoints::{DiscoverableProtocolMarker, RequestStream};
use fidl_fuchsia_recovery_ui::{
    ProgressRendererMarker, ProgressRendererRequest, ProgressRendererRequestStream, Status,
};
use fuchsia_async as fasync;
use fuchsia_zircon::{Duration, Event};
use futures::prelude::*;
#[cfg(feature = "debug_touch_to_update")]
use rand;
#[cfg(not(feature = "debug_touch_to_update"))]
use rand as _;
use recovery_util::ui::progress_bar::{
    ProgressBar, ProgressBarConfig, ProgressBarMessages, ProgressBarText,
};
use rive_rs::File;
#[cfg(feature = "debug_touch_to_update")]
use std::time::Instant;
use tracing::{error, info};

const LOGO_IMAGE_PATH: &str = "/pkg/data/logo.riv";
const BG_COLOR: Color = Color::white();
const SHOW_PROGRESS_TEXT: bool = false;

pub struct ProgressBarViewAssistant {
    app_sender: AppSender,
    view_key: ViewKey,
    scene: Option<Scene>,
    logo_file: Option<File>,
    progress_bar: Option<ProgressBar>,
    percent_complete: f32,
    #[cfg(feature = "debug_touch_to_update")]
    touch_time: Instant,
}

impl ProgressBarViewAssistant {
    pub fn new(
        app_sender: AppSender,
        view_key: ViewKey,
        percent_complete: f32,
    ) -> Result<ProgressBarViewAssistant, Error> {
        let logo_file = load_rive(LOGO_IMAGE_PATH).ok();
        Ok(ProgressBarViewAssistant {
            app_sender: app_sender.clone(),
            view_key,
            scene: None,
            logo_file,
            progress_bar: None,
            percent_complete,
            #[cfg(feature = "debug_touch_to_update")]
            touch_time: Instant::now(),
        })
    }
}

impl ViewAssistant for ProgressBarViewAssistant {
    fn setup(&mut self, _context: &ViewAssistantContext) -> Result<(), Error> {
        Ok(())
    }

    fn resize(&mut self, _new_size: &Size) -> Result<(), Error> {
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        if self.scene.is_none() {
            let target_size = view_context.size;
            let min_dimension = target_size.width.min(target_size.height);
            let logo_edge = min_dimension * 0.24;
            let text_size = min_dimension / 14.0;

            let mut builder =
                SceneBuilder::new().background_color(BG_COLOR).round_scene_corners(true);

            builder
                .group()
                .column()
                .max_size()
                .main_align(MainAxisAlignment::SpaceEvenly)
                .cross_align(CrossAxisAlignment::Center)
                .contents(|builder| {
                    let progress_bar_size =
                        size2(target_size.width / 2.0, target_size.height / 30.0);
                    let progress_config = if SHOW_PROGRESS_TEXT {
                        ProgressBarConfig::TextWithSize(
                            ProgressBarText {
                                text: format!(
                                    "Progress Bar {}%   ",
                                    (self.percent_complete * 100.0) as i32
                                ),
                                font_size: text_size,
                                padding: 7.0,
                                alignment: TextHorizontalAlignment::Left,
                            },
                            progress_bar_size,
                        )
                    } else {
                        ProgressBarConfig::Size(progress_bar_size)
                    };

                    builder.space(size2(target_size.width, target_size.height / 5.0));
                    let logo_size: Size = size2(logo_edge, logo_edge);
                    if let Some(logo_file) = self.logo_file.as_ref() {
                        let facet = RiveFacet::new_from_file(logo_size, logo_file, None)
                            .expect("Error creating logo facet from file");
                        builder.facet(Box::new(facet));
                    }
                    let progress_bar =
                        ProgressBar::new(progress_config, builder, self.app_sender.clone())
                            .expect("Progress Bar");
                    self.progress_bar = Some(progress_bar);
                    builder.space(size2(target_size.width, target_size.height / 5.0));
                });
            let mut scene = builder.build();
            scene.layout(target_size);
            self.scene = Some(scene);
        }
        if let Some(scene) = &mut self.scene {
            scene.render(render_context, ready_event, view_context)?;
            view_context.request_render();
        }
        Ok(())
    }

    fn handle_message(&mut self, message: Message) {
        if message.is::<ProgressBarMessages>() {
            if let (Some(progress_bar), Some(scene)) = (&mut self.progress_bar, &mut self.scene) {
                let message = message.downcast::<ProgressBarMessages>().unwrap();
                progress_bar.handle_message(scene, self.view_key, *message);
            }
        }
    }

    #[cfg(feature = "debug_touch_to_update")]
    fn handle_touch_event(
        &mut self,
        _context: &mut ViewAssistantContext,
        _event: &input::Event,
        _touch_event: &input::touch::Event,
    ) -> Result<(), Error> {
        // Debounce the screen touch
        if self.touch_time.elapsed().as_millis() > 100 {
            let rand_percent = rand::random::<f32>() * 100.0;
            self.app_sender.queue_message(
                MessageTarget::View(self.view_key),
                make_message(ProgressBarMessages::SetProgressSmooth(
                    rand_percent,
                    Duration::from_seconds(1),
                )),
            );
            let text = format!("Progress Bar {}%   ", rand_percent as i32);
            self.app_sender.queue_message(
                MessageTarget::View(self.view_key),
                make_message(ProgressBarMessages::SetProgressBarText(text)),
            );
        }
        self.touch_time = Instant::now();
        Ok(())
    }
}

struct ProgressBarAppAssistant {
    app_sender: AppSender,
    progress_view_key: Option<ViewKey>,
}

impl ProgressBarAppAssistant {
    pub fn new(app_sender: &AppSender) -> Self {
        Self { app_sender: app_sender.clone(), progress_view_key: None }
    }

    fn handle_progress_update(
        app_context: &AppSender,
        status: Status,
        percent_complete: f32,
        elapsed_time: Option<Duration>,
    ) {
        match status {
            Status::Active => {
                let progress = percent_complete / 100.0;
                let progress_message = if elapsed_time.is_some()
                    && elapsed_time.unwrap() > Duration::from_seconds(0)
                {
                    ProgressBarMessages::SetProgressSmooth(progress, elapsed_time.unwrap())
                } else {
                    ProgressBarMessages::SetProgress(progress)
                };
                app_context
                    .queue_message(MessageTarget::Application, make_message(progress_message));
            }
            Status::Complete => {
                app_context.queue_message(
                    MessageTarget::Application,
                    make_message(ProgressBarMessages::SetProgress(1.0)),
                );
            }
            _ => error!("Unhandled progress update {:?}", status),
        };
    }
}

impl AppAssistant for ProgressBarAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        info!("AppAssistant setup");
        Ok(())
    }

    fn create_view_assistant(&mut self, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
        info!("Create view assistant");
        self.progress_view_key = Some(view_key.clone());
        Ok(Box::new(ProgressBarViewAssistant::new(self.app_sender.clone(), view_key, 0.0)?))
    }

    fn filter_config(&mut self, config: &mut Config) {
        config.display_rotation = DisplayRotation::Deg90;
    }

    fn outgoing_services_names(&self) -> Vec<&'static str> {
        vec![ProgressRendererMarker::PROTOCOL_NAME]
    }

    fn handle_message(&mut self, message: Message) {
        if message.is::<ProgressBarMessages>() {
            if let Some(view_key) = self.progress_view_key {
                self.app_sender.queue_message(MessageTarget::View(view_key), message);
            }
        }
    }

    fn handle_service_connection_request(
        &mut self,
        service_name: &str,
        channel: fasync::Channel,
    ) -> Result<(), Error> {
        info!("Handle Service Connection Request");
        match service_name {
            ProgressRendererMarker::PROTOCOL_NAME => {
                let app_sender = self.app_sender.clone();
                fasync::Task::local(async move {
                    let stream = ProgressRendererRequestStream::from_channel(channel);
                    stream
                        .try_fold(app_sender, move |local_app_sender, result| async move {
                            match result {
                                ProgressRendererRequest::Render {
                                    status,
                                    percent_complete,
                                    responder,
                                } => {
                                    ProgressBarAppAssistant::handle_progress_update(
                                        &local_app_sender,
                                        status,
                                        percent_complete,
                                        None,
                                    );
                                    responder.send().expect("Error replying to progress update");
                                }
                                ProgressRendererRequest::Render2 { payload, responder } => {
                                    if let Some(status) = payload.status {
                                        let elapsed_time = if let Some(nanos) = payload.elapsed_time
                                        {
                                            Some(Duration::from_nanos(nanos))
                                        } else {
                                            None
                                        };
                                        let mut percent_complete =
                                            payload.percent_complete.unwrap_or_else(|| 0.0);
                                        if percent_complete.is_nan() {
                                            percent_complete = 0.0;
                                        }
                                        ProgressBarAppAssistant::handle_progress_update(
                                            &local_app_sender,
                                            status,
                                            percent_complete,
                                            elapsed_time,
                                        );
                                    }
                                    responder.send().expect("Error replying to progress update");
                                }
                            }
                            Ok(local_app_sender)
                        })
                        .await
                        .expect("Failed to process progress events");
                })
                .detach();
            }
            _ => panic!("Error: Unexpected service: {}", service_name),
        }
        Ok(())
    }
}

fn make_app_assistant_fut(
    app_sender: &AppSender,
) -> LocalBoxFuture<'_, Result<AppAssistantPtr, Error>> {
    info!("make_app_assistant_fut");
    let f = async move {
        info!("in async move");
        let assistant = Box::new(ProgressBarAppAssistant::new(app_sender));
        Ok::<AppAssistantPtr, Error>(assistant)
    };
    Box::pin(f)
}

pub fn make_app_assistant() -> AssistantCreatorFunc {
    info!("Make app assistant");
    Box::new(make_app_assistant_fut)
}

#[fuchsia::main(logging = true)]
fn main() -> Result<(), Error> {
    App::run(make_app_assistant())
}
