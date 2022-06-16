// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::{
    color::Color,
    drawing::{load_font, measure_text_width, FontFace},
    make_message,
    scene::{
        facets::{
            FacetId, SetColorMessage, SetSizeMessage, SetTextMessage, TextFacetOptions,
            TextHorizontalAlignment, TextVerticalAlignment,
        },
        layout::{Alignment, Stack, StackOptions},
        scene::{Scene, SceneBuilder},
    },
    AppSender, MessageTarget, Point, Size, ViewKey,
};
use euclid::size2;
use fuchsia_async as fasync;
use futures::{
    channel::mpsc::{channel as pipe, Sender},
    StreamExt,
};
use std::{
    path::PathBuf,
    sync::{
        atomic::{AtomicU32, Ordering},
        Arc, Mutex,
    },
    time::Duration,
};

const PROGRESS_GRANULARITY: f32 = 1000.0;

// We calculate the size from a provided size or displayed text, we must have at least one
pub enum ProgressBarConfig {
    Text(ProgressBarText),
    Size(Size),
    TextWithSize(ProgressBarText, Size),
}

pub enum ProgressBarMessages {
    SetProgress(f32),
    SetProgressSmooth(f32),
    SetInternalProgress(f32),
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
    pub text: String,
    pub font_size: f32,
    pub padding: f32,
    pub alignment: TextHorizontalAlignment,
}

impl ProgressBar {
    pub fn new(
        percent_complete: f32,
        config: ProgressBarConfig,
        builder: &mut SceneBuilder,
        app_sender: AppSender,
    ) -> Result<ProgressBar, Error> {
        let options =
            StackOptions { alignment: Alignment::center_left(), ..StackOptions::default() };

        // Construct the progress bar itself
        builder.start_group("progress_bar", Stack::with_options_ptr(options));

        let (label, progress_bar_size) = build_progress_text(config, builder)?;
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
    pub fn set_percent(&mut self, scene: &mut Scene, percent_filled: f32) {
        self.set_progress(scene, percent_filled / 100.0);
    }

    /// Set progress from 0.0 to 1.0
    pub fn set_progress(&mut self, scene: &mut Scene, progress: f32) {
        self.final_progress.store((progress * PROGRESS_GRANULARITY) as u32, Ordering::Release);
        self.current_progress.store((progress * PROGRESS_GRANULARITY) as u32, Ordering::Release);
        self.set_internal_progress(scene, progress);
    }

    /// Set progress from 0 to 1.0
    pub fn set_internal_progress(&mut self, scene: &mut Scene, progress: f32) {
        let progress = num_traits::clamp(progress, 0.0, 1.0);
        let width = self.progress_bar_size.width;
        let height = self.progress_bar_size.height;
        let filled = width * progress;
        scene.send_message(
            &self.progress_rectangle,
            Box::new(SetSizeMessage { size: size2(filled, height) }),
        );
    }

    pub fn set_percent_smooth(&mut self, view_key: ViewKey, percent_filled: f32) {
        self.set_progress_smooth(view_key, percent_filled / 100.0);
    }

    /// Set progress from 0.0 to 1.0
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
                            Err(_) => { // Ignore
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

    pub fn set_color(&mut self, scene: &mut Scene, fg_color: Color, bg_color: Color) {
        scene.send_message(&self.progress_rectangle, Box::new(SetColorMessage { color: fg_color }));
        scene.send_message(
            &self.background_rectangle,
            Box::new(SetColorMessage { color: bg_color }),
        );
    }

    pub fn set_text(&mut self, scene: &mut Scene, text: String) {
        if let Some(label) = self.label.as_ref() {
            scene.send_message(label, Box::new(SetTextMessage { text }));
        }
    }

    pub fn set_text_color(&mut self, scene: &mut Scene, color: Color) {
        if let Some(label) = self.label.as_ref() {
            scene.send_message(label, Box::new(SetColorMessage { color }));
        }
    }
}

fn build_text_facet(
    builder: &mut SceneBuilder,
    progress_bar_text: &ProgressBarText,
    face: FontFace,
) -> Result<FacetId, Error> {
    // Result<(FacetId, &'a FontFace), Error> {
    // let face = load_font(PathBuf::from("/pkg/data/fonts/Roboto-Regular.ttf"))?;
    let label = builder.text(
        face,
        &progress_bar_text.text,
        progress_bar_text.font_size,
        Point::zero(),
        TextFacetOptions {
            color: Color::blue(),
            horizontal_alignment: progress_bar_text.alignment,
            vertical_alignment: TextVerticalAlignment::Top,
            ..TextFacetOptions::default()
        },
    );
    Ok(label)
}

fn build_progress_text(
    config: ProgressBarConfig,
    builder: &mut SceneBuilder,
) -> Result<(Option<FacetId>, Size), Error> {
    Ok(match config {
        ProgressBarConfig::Text(progress_bar_text) => {
            let face = load_font(PathBuf::from("/pkg/data/fonts/Roboto-Regular.ttf"))?;
            let label_width =
                measure_text_width(&face, progress_bar_text.font_size, &progress_bar_text.text);
            let width = label_width + progress_bar_text.padding * 2.0;
            let height = progress_bar_text.font_size + progress_bar_text.padding * 2.0;
            let size = size2(width, height);
            let label = build_text_facet(builder, &progress_bar_text, face)?;
            (Some(label), size)
        }
        ProgressBarConfig::TextWithSize(progress_bar_text, size) => {
            let face = load_font(PathBuf::from("/pkg/data/fonts/Roboto-Regular.ttf"))?;
            let label = build_text_facet(builder, &progress_bar_text, face)?;
            (Some(label), size)
        }
        ProgressBarConfig::Size(size) => (None, size),
    })
}
