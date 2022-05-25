// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::scene::facets::SetTextMessage;
use carnelian::{
    color::Color,
    drawing::{load_font, measure_text_width},
    make_message,
    scene::{
        facets::{
            FacetId, SetColorMessage, SetSizeMessage, TextFacetOptions, TextHorizontalAlignment,
            TextVerticalAlignment,
        },
        layout::{Alignment, Stack, StackOptions},
        scene::{Scene, SceneBuilder},
    },
    AppSender, MessageTarget, Point, Size, ViewKey,
};
use euclid::size2;
use fuchsia_async::futures::channel::mpsc::{channel as pipe, Sender};
use fuchsia_async::futures::StreamExt;
use fuchsia_async::{self as fasync};
use std::sync::Mutex;
use std::time::Duration;
use std::{
    path::PathBuf,
    sync::{
        atomic::{AtomicU32, Ordering},
        Arc,
    },
};

const PROGRESS_GRANULARITY: f32 = 1000.0;

pub enum ProgressBarMessages {
    #[allow(unused)]
    SetProgress(f32),
    #[allow(unused)]
    SetProgressSmooth(f32),
    SetInternalProgress(f32),
    #[allow(unused)]
    SetProgressBarText(String),
}

pub struct ProgressBar {
    label: Option<FacetId>,
    progress_bar_size: Size,
    progress_rectangle: FacetId,
    background_rectangle: FacetId,
    app_sender: AppSender,
    // Progress stored as 0 to PROGRESS_GRANULARITY
    current_progress: Arc<AtomicU32>,
    // Progress stored as 0 to PROGRESS_GRANULARITY
    final_progress: Arc<AtomicU32>,
    // To make sure that we are only starting one copy of the background task
    task_running_mutex: Arc<Mutex<bool>>,
    // Channel to send desired progress to the beckground task
    task_sender: Option<Sender<f32>>,
}

#[derive(Clone)]
pub struct ProgressBarText {
    text: String,
    font_size: f32,
    padding: f32,
    alignment: TextHorizontalAlignment,
}

impl ProgressBar {
    pub fn new(
        percent_complete: f32,
        text: Option<ProgressBarText>,
        mut size: Option<Size>,
        builder: &mut SceneBuilder,
        app_sender: AppSender,
    ) -> Result<ProgressBar, Error> {
        // We calculate the size from a size option or from displayed text, we must have at least one
        assert!(text.is_some() || size.is_some());
        let options =
            StackOptions { alignment: Alignment::center_left(), ..StackOptions::default() };

        // Construct the progress bar itself
        builder.start_group("progress_bar", Stack::with_options_ptr(options));

        let mut label = None;
        if let Some(progress_bar_text) = text {
            let face = load_font(PathBuf::from("/pkg/data/fonts/Roboto-Regular.ttf"))?;
            label = Some(builder.text(
                face.clone(),
                &progress_bar_text.text,
                progress_bar_text.font_size,
                Point::zero(),
                TextFacetOptions {
                    color: Color::blue(),
                    horizontal_alignment: progress_bar_text.alignment,
                    vertical_alignment: TextVerticalAlignment::Top,
                    ..TextFacetOptions::default()
                },
            ));
            if size.is_none() {
                let label_width =
                    measure_text_width(&face, progress_bar_text.font_size, &progress_bar_text.text);
                let width = label_width + progress_bar_text.padding * 2.0;
                let height = progress_bar_text.font_size + progress_bar_text.padding * 2.0;
                size = Some(size2(width, height));
            }
        }

        // We have to have Some(size) by this point
        let progress_bar_size = size.unwrap();
        let filled = progress_bar_size.width * percent_complete / 100.0;
        let progress_rectangle =
            builder.rectangle(size2(filled, progress_bar_size.height), Color::green());
        let background_rectangle =
            builder.rectangle(progress_bar_size, Color::from_hash_code("#000000")?);
        builder.end_group();

        Ok(ProgressBar {
            label,
            progress_bar_size,
            progress_rectangle,
            background_rectangle,
            app_sender,
            current_progress: Arc::new(AtomicU32::new(0)),
            final_progress: Arc::new(AtomicU32::new(0)),
            task_running_mutex: Arc::new(Mutex::new(false)),
            task_sender: None,
        })
    }

    /// Set progress between 0 to 100 percent
    #[allow(unused)]
    pub fn set_percent(&mut self, scene: &mut Scene, percent_filled: f32) {
        self.set_progress(scene, percent_filled / 100.0);
    }

    /// Set progress from 0.0 to 1.0
    #[allow(unused)]
    pub fn set_progress(&mut self, scene: &mut Scene, progress: f32) {
        println!("Set progress {}", progress);
        self.final_progress.store((progress * PROGRESS_GRANULARITY) as u32, Ordering::Release);
        self.current_progress.store((progress * PROGRESS_GRANULARITY) as u32, Ordering::Release);
        self.set_internal_progress(scene, progress);
    }

    /// Set progress from 0 to 1.0
    fn set_internal_progress(&mut self, scene: &mut Scene, progress: f32) {
        let progress = num_traits::clamp(progress, 0.0, 1.0);
        let width = self.progress_bar_size.width;
        let height = self.progress_bar_size.height;
        let filled = width * progress;
        scene.send_message(
            &self.progress_rectangle,
            Box::new(SetSizeMessage { size: size2(filled, height) }),
        );
    }

    #[allow(unused)]
    pub fn set_percent_smooth(&mut self, view_key: ViewKey, percent_filled: f32) {
        self.set_progress_smooth(view_key, percent_filled / 100.0);
    }

    /// Set progress from 0.0 to 1.0
    #[allow(unused)]
    pub fn set_progress_smooth(&mut self, view_key: ViewKey, progress: f32) {
        let mut running = self.task_running_mutex.lock().unwrap();
        if !*running {
            *running = true;
            let app_sender = self.app_sender.clone();
            let current_progress = self.current_progress.clone();
            let final_progress = self.final_progress.clone();
            let (tx, mut rx) = pipe::<f32>(1);
            self.task_sender = Some(tx);
            let f = async move {
                let sleep_time = Duration::from_millis(5);
                loop {
                    let current = current_progress.load(Ordering::Acquire);

                    let end = final_progress.load(Ordering::Acquire);
                    let difference = end as i64 - current as i64;
                    if difference == 0 {
                        let progress = rx.next().await.unwrap();
                        final_progress
                            .store((progress * PROGRESS_GRANULARITY) as u32, Ordering::Release);
                    } else {
                        match rx.try_next() {
                            Ok(value) => {
                                if let Some(progress) = value {
                                    final_progress.store(
                                        (progress * PROGRESS_GRANULARITY) as u32,
                                        Ordering::Release,
                                    );
                                }
                            }
                            Err(_) => { // Ignore}
                            }
                        }
                        if difference > 0 {
                            current_progress.fetch_add(1, Ordering::AcqRel);
                        } else {
                            current_progress.fetch_sub(1, Ordering::AcqRel);
                        }
                        app_sender.queue_message(
                            MessageTarget::View(view_key),
                            make_message(ProgressBarMessages::SetInternalProgress(
                                current_progress.load(Ordering::Acquire) as f32
                                    / PROGRESS_GRANULARITY,
                            )),
                        );
                        fuchsia_async::Timer::new(sleep_time).await;
                    }
                }
            };
            fasync::Task::local(f).detach();
        }
        let _ = self.task_sender.as_mut().unwrap().start_send(progress);
    }

    #[allow(unused)]
    fn set_color(&mut self, scene: &mut Scene, fg_color: Color, bg_color: Color) {
        scene.send_message(&self.progress_rectangle, Box::new(SetColorMessage { color: fg_color }));
        scene.send_message(
            &self.background_rectangle,
            Box::new(SetColorMessage { color: bg_color }),
        );
    }

    #[allow(unused)]
    fn set_text(&mut self, scene: &mut Scene, text: String) {
        if let Some(label) = self.label.as_ref() {
            scene.send_message(label, Box::new(SetTextMessage { text }));
        }
    }

    #[allow(unused)]
    fn set_text_color(&mut self, scene: &mut Scene, color: Color) {
        if let Some(label) = self.label.as_ref() {
            scene.send_message(label, Box::new(SetColorMessage { color }));
        }
    }
}

// Example code showing how to use the progress bar.
//
// To run the example:
//
// 1. In the BUILD.gn file add next to main.rs (two places)
//      "src/progress_bar.rs",
//
// 2. Comment out the annotation below:
//      #[cfg(progress_bar_stand_alone)]
//
// 3. change main.rs::main() line 990 to
//
//      mod progress_bar;
//      fn main() -> Result<(), Error> {
//          println!("recovery: Progress bar started");
//          App::run(progress_bar::stand_alone::make_app_assistant())
//      }
//
// 4. Build and run recovery as normal:
//      fx build build/images/recovery
//
// 5. Touch the screen to see the progress bar progressing.

//#[cfg(progress_bar_stand_alone)]
pub(crate) mod stand_alone {
    use crate::progress_bar::{ProgressBar, ProgressBarMessages, ProgressBarText};
    use crate::BG_COLOR;
    use anyhow::Error;
    use carnelian::app::Config;
    use carnelian::drawing::DisplayRotation;
    use carnelian::render::rive::load_rive;
    use carnelian::scene::facets::{RiveFacet, TextHorizontalAlignment};
    use carnelian::scene::layout::CrossAxisAlignment; //Flex, FlexOptions, MainAxisSize};
    use carnelian::{
        input, make_message,
        render::Context,
        scene::{
            layout::MainAxisAlignment,
            scene::{Scene, SceneBuilder},
        },
        App, AppAssistant, AppAssistantPtr, AppSender, AssistantCreatorFunc, LocalBoxFuture,
        Message, MessageTarget, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr,
        ViewKey,
    };
    use euclid::{size2, Size2D};
    use fuchsia_zircon::Event;
    use rive_rs::File;
    use std::time::Instant;

    const LOGO_IMAGE_PATH: &str = "/pkg/data/logo.riv";

    // This assistant is for using as an example
    pub struct ProgressBarViewAssistant {
        app_sender: AppSender,
        view_key: ViewKey,
        scene: Option<Scene>,
        logo_file: Option<File>,
        progress_bar: Option<ProgressBar>,
        percent_complete: f32,
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
            render_context: &mut Context,
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
                let text = ProgressBarText {
                    text: format!("Progress Bar {}%   ", (self.percent_complete * 100.0) as i32),
                    font_size: text_size,
                    padding: 7.0,
                    alignment: TextHorizontalAlignment::Left,
                };
                builder
                    .group()
                    .column()
                    .max_size()
                    .main_align(MainAxisAlignment::SpaceEvenly)
                    .cross_align(CrossAxisAlignment::Center)
                    .contents(|builder| {
                        let logo_size: Size = size2(logo_edge, logo_edge);
                        if let Some(logo_file) = self.logo_file.as_ref() {
                            let facet = RiveFacet::new_from_file(logo_size, logo_file, None)
                                .expect("facet_from_file");
                            builder.facet(Box::new(facet));
                        }
                        let progress_bar = ProgressBar::new(
                            self.percent_complete,
                            Some(text.clone()),
                            Some(size2(target_size.width / 2.0, target_size.height / 15.0)),
                            builder,
                            self.app_sender.clone(),
                        )
                        .expect("Progress Bar");
                        self.progress_bar = Some(progress_bar);
                        builder.space(Size2D {
                            width: target_size.width,
                            height: target_size.height / 5.0,
                            _unit: Default::default(),
                        });
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
                let proxy_message = message.downcast::<ProgressBarMessages>().unwrap();
                match *proxy_message {
                    ProgressBarMessages::SetProgress(progress) => {
                        if let (Some(progress_bar), Some(scene)) =
                            (&mut self.progress_bar, &mut self.scene)
                        {
                            self.percent_complete = progress * 100.0;
                            progress_bar.set_progress(scene, progress);
                        }
                    }
                    ProgressBarMessages::SetProgressSmooth(progress) => {
                        if let Some(progress_bar) = &mut self.progress_bar {
                            self.percent_complete = progress * 100.0;
                            progress_bar.set_progress_smooth(self.view_key, progress);
                        }
                    }
                    // This is used by the slow progress task and needs to be included in code as-is
                    ProgressBarMessages::SetInternalProgress(progress) => {
                        if let (Some(progress_bar), Some(scene)) =
                            (&mut self.progress_bar, &mut self.scene)
                        {
                            progress_bar.set_internal_progress(scene, progress);
                        }
                    }
                    ProgressBarMessages::SetProgressBarText(text) => {
                        if let (Some(progress_bar), Some(scene)) =
                            (&mut self.progress_bar, &mut self.scene)
                        {
                            progress_bar.set_text(scene, text);
                        }
                    }
                }
            }
        }

        fn handle_touch_event(
            &mut self,
            _context: &mut ViewAssistantContext,
            _event: &input::Event,
            _touch_event: &input::touch::Event,
        ) -> Result<(), Error> {
            // Debounce the screen touch
            if self.touch_time.elapsed().as_millis() > 100 {
                let r = rand::random::<f32>();
                self.app_sender.queue_message(
                    MessageTarget::View(self.view_key),
                    make_message(ProgressBarMessages::SetProgressSmooth(r)),
                );
                let text = format!("Progress Bar {}%", (r * 100.0) as i32);
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
    }

    impl ProgressBarAppAssistant {
        pub fn new(app_sender: &AppSender) -> Self {
            Self { app_sender: app_sender.clone() }
        }
    }

    impl AppAssistant for ProgressBarAppAssistant {
        fn setup(&mut self) -> Result<(), Error> {
            Ok(())
        }

        fn create_view_assistant(&mut self, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
            Ok(Box::new(ProgressBarViewAssistant::new(self.app_sender.clone(), view_key, 0.0)?))
        }

        fn filter_config(&mut self, config: &mut Config) {
            config.display_rotation = DisplayRotation::Deg90;
        }
    }

    fn make_app_assistant_fut(
        app_sender: &AppSender,
    ) -> LocalBoxFuture<'_, Result<AppAssistantPtr, Error>> {
        let f = async move {
            let assistant = Box::new(ProgressBarAppAssistant::new(app_sender));
            Ok::<AppAssistantPtr, Error>(assistant)
        };
        Box::pin(f)
    }

    pub fn make_app_assistant() -> AssistantCreatorFunc {
        Box::new(make_app_assistant_fut)
    }

    #[allow(unused)]
    fn main() -> Result<(), Error> {
        App::run(make_app_assistant())
    }
}
