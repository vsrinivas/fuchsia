// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::scene::facets::TextHorizontalAlignment;
use carnelian::{
    color::Color,
    drawing::{load_font, measure_text_width},
    scene::{
        facets::{
            FacetId, SetColorMessage, SetSizeMessage, TextFacetOptions, TextVerticalAlignment,
        },
        layout::{Alignment, Stack, StackOptions},
        scene::{Scene, SceneBuilder},
    },
    Point, Size,
};
use euclid::size2;
use std::path::PathBuf;

pub struct ProgressBar {
    label: Option<FacetId>,
    progress_bar_size: Size,
    progress_rectangle: FacetId,
    background_rectangle: FacetId,
}

#[derive(Copy, Clone)]
pub struct ProgressBarText {
    text: &'static str,
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
                progress_bar_text.text,
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
                    measure_text_width(&face, progress_bar_text.font_size, progress_bar_text.text);
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

        Ok(ProgressBar { label, progress_bar_size, progress_rectangle, background_rectangle })
    }

    fn clamp<T: std::cmp::PartialOrd>(value: T, min: T, max: T) -> T {
        if value < min {
            min
        } else if value > max {
            max
        } else {
            value
        }
    }

    #[allow(unused)]
    pub fn set_percent(&mut self, scene: &mut Scene, percent_filled: f32) {
        self.set_progress(scene, percent_filled / 100.0);
    }

    #[allow(unused)]
    pub fn set_progress(&mut self, scene: &mut Scene, progress: f32) {
        let progress = ProgressBar::clamp(progress, 0.0, 1.0);
        let width = self.progress_bar_size.width;
        let height = self.progress_bar_size.height;
        let filled = width * progress;
        scene.send_message(
            &self.progress_rectangle,
            Box::new(SetSizeMessage { size: size2(filled, height) }),
        );
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
    use crate::progress_bar::{ProgressBar, ProgressBarText};
    use crate::BG_COLOR;
    use anyhow::Error;
    use carnelian::app::Config;
    use carnelian::drawing::DisplayRotation;
    use carnelian::scene::facets::TextHorizontalAlignment;
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
    use euclid::size2;
    use fuchsia_async::{self as fasync, DurationExt};
    use fuchsia_zircon::{Duration, Event};

    pub enum ProgressBarMessages {
        SetProgress(f32),
        EndTask,
    }

    // This assistant is for using as an example
    pub struct ProgressBarViewAssistant {
        app_sender: AppSender,
        view_key: ViewKey,
        scene: Option<Scene>,
        progress_bar: Option<ProgressBar>,
        percent_complete: f32,
        demo_running: bool,
    }

    impl ProgressBarViewAssistant {
        pub fn new(
            app_sender: AppSender,
            view_key: ViewKey,
            percent_complete: f32,
        ) -> Result<ProgressBarViewAssistant, Error> {
            Ok(ProgressBarViewAssistant {
                app_sender: app_sender.clone(),
                view_key,
                scene: None,
                progress_bar: None,
                percent_complete,
                demo_running: false,
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
                let text_size = min_dimension / 14.0;

                let mut builder =
                    SceneBuilder::new().background_color(BG_COLOR).round_scene_corners(true);
                let text = ProgressBarText {
                    text: "Progress Bar",
                    font_size: text_size,
                    padding: 7.0,
                    alignment: TextHorizontalAlignment::Right,
                };
                builder
                    .group()
                    .column()
                    .max_size()
                    .main_align(MainAxisAlignment::SpaceEvenly)
                    .contents(|builder| {
                        let progress_bar = ProgressBar::new(
                            self.percent_complete,
                            Some(text),
                            Some(size2(target_size.width / 2.0, target_size.height / 15.0)),
                            builder,
                        )
                        .expect("Progress Bar");
                        self.progress_bar = Some(progress_bar);
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
                    ProgressBarMessages::EndTask => {
                        self.demo_running = false;
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
            if !self.demo_running {
                self.demo_running = true;
                let view_key = self.view_key;
                let app_sender = self.app_sender.clone();
                let f = async move {
                    let sleep_time = Duration::from_millis(50);
                    let mut count = 0.0;
                    while count <= 1.0 {
                        app_sender.queue_message(
                            MessageTarget::View(view_key),
                            make_message(ProgressBarMessages::SetProgress(count as f32)),
                        );
                        fuchsia_async::Timer::new(sleep_time.after_now()).await;
                        count += 0.0025;
                    }
                    app_sender.queue_message(
                        MessageTarget::View(view_key),
                        make_message(ProgressBarMessages::EndTask),
                    );
                };
                fasync::Task::local(f).detach();
            }
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
