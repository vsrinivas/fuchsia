// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::{
    app::{Config, FrameBufferPtr, MessageInternal},
    drawing::DisplayRotation,
    geometry::{IntPoint, IntSize, Size},
    input,
    message::Message,
    render::{
        generic::{self, Backend},
        Context, ContextInner,
    },
    view::{
        strategies::base::{ViewStrategy, ViewStrategyPtr},
        ViewAssistantContext, ViewAssistantPtr, ViewDetails, ViewKey,
    },
};
use anyhow::{ensure, Error};
use async_trait::async_trait;
use euclid::size2;
use fuchsia_async::{self as fasync};
use fuchsia_framebuffer::{
    FrameCollection, FrameCollectionBuilder, FrameSet, FrameUsage, ImageId, ImageInCollection,
};
use fuchsia_trace::{self, duration, instant};
use fuchsia_zircon::{self as zx, Duration, Event, HandleBased, Time};
use futures::{
    channel::mpsc::{unbounded, UnboundedSender},
    StreamExt,
};
use std::collections::BTreeMap;

type WaitEvents = BTreeMap<ImageId, Event>;

struct DisplayResources {
    pub frame_set: FrameSet,
    pub frame_collection: FrameCollection,
    pub image_indexes: BTreeMap<ImageId, u32>,
    pub wait_events: WaitEvents,
    pub context: Context,
    pub pixel_format: fuchsia_framebuffer::PixelFormat,
}

pub(crate) struct FrameBufferViewStrategy {
    app_sender: UnboundedSender<MessageInternal>,
    display_rotation: DisplayRotation,
    frame_buffer: FrameBufferPtr,
    display_resources: Option<DisplayResources>,
    image_sender: futures::channel::mpsc::UnboundedSender<ImageInCollection>,
    vsync_phase: Time,
    vsync_interval: Duration,
    mouse_cursor_position: Option<IntPoint>,
    drop_display_resources_task: Option<fasync::Task<()>>,
    size: IntSize,
    pub collection_id: u64,
    pixel_format: fuchsia_framebuffer::PixelFormat,
    display_resource_release_delay: std::time::Duration,
    render_frame_count: usize,
    presented: Option<u64>,
}

const RENDER_FRAME_COUNT: usize = 2;

impl FrameBufferViewStrategy {
    async fn allocate_frames(
        _pixel_format: fuchsia_framebuffer::PixelFormat,
        frame_collection: &FrameCollection,
    ) -> Result<(BTreeMap<ImageId, u32>, WaitEvents, FrameSet), Error> {
        let fb_image_ids = frame_collection.get_image_ids();
        let mut image_indexes = BTreeMap::new();
        let mut wait_events: WaitEvents = WaitEvents::new();
        for frame_image_id in &fb_image_ids {
            let frame = frame_collection.get_frame(*frame_image_id);
            let frame_index = frame.image_index;
            image_indexes.insert(*frame_image_id, frame_index);
            let wait_event = frame
                .wait_event
                .duplicate_handle(zx::Rights::SAME_RIGHTS)
                .expect("duplicate_handle");
            wait_events.insert(frame.image_id, wait_event);
        }
        let frame_set = FrameSet::new(frame_collection.collection_id, fb_image_ids);
        Ok((image_indexes, wait_events, frame_set))
    }

    pub(crate) async fn new(
        key: ViewKey,
        display_rotation: DisplayRotation,
        size: &IntSize,
        pixel_format: fuchsia_framebuffer::PixelFormat,
        app_sender: UnboundedSender<MessageInternal>,
        frame_buffer: FrameBufferPtr,
        display_resource_release_delay: std::time::Duration,
    ) -> Result<ViewStrategyPtr, Error> {
        let collection_id = 1;
        let render_frame_count = Config::get().buffer_count.unwrap_or(RENDER_FRAME_COUNT);
        ensure!(render_frame_count > 0, "buffer_count from config must be greater than zero");
        app_sender.unbounded_send(MessageInternal::Render(key)).expect("unbounded_send");
        app_sender.unbounded_send(MessageInternal::Focus(key)).expect("unbounded_send");

        let display_resources = Self::allocate_display_resources(
            collection_id,
            Config::get().use_spinel,
            *size,
            &frame_buffer,
            pixel_format,
            display_rotation,
            render_frame_count,
        )
        .await?;

        let mut fb = frame_buffer.borrow_mut();
        let config_for_format = fb.get_config_for_format(display_resources.pixel_format);
        fb.configure_layer(&config_for_format, display_resources.frame_collection.image_type())?;

        let image_freed_sender = app_sender.clone();
        let (image_sender, mut image_receiver) = unbounded::<ImageInCollection>();
        // wait for events from the image freed fence to know when an
        // image can prepared.
        fasync::Task::local(async move {
            while let Some(image_in_collection) = image_receiver.next().await {
                image_freed_sender
                    .unbounded_send(MessageInternal::ImageFreed(
                        key,
                        image_in_collection.image_id,
                        image_in_collection.collection_id as u32,
                    ))
                    .expect("unbounded_send");
            }
        })
        .detach();

        Ok(Box::new(FrameBufferViewStrategy {
            app_sender,
            display_rotation,
            frame_buffer: frame_buffer.clone(),
            display_resources: Some(display_resources),
            image_sender: image_sender,
            vsync_phase: Time::get_monotonic(),
            vsync_interval: Duration::from_millis(16),
            mouse_cursor_position: None,
            drop_display_resources_task: None,
            size: *size,
            pixel_format,
            collection_id,
            display_resource_release_delay,
            render_frame_count,
            presented: None,
        }))
    }

    fn make_context(
        &mut self,
        view_details: &ViewDetails,
        image_id: Option<ImageId>,
    ) -> ViewAssistantContext {
        let time_now = Time::get_monotonic();
        // |interval_offset| is the offset from |time_now| to the next multiple
        // of vsync interval after vsync phase, possibly negative if in the past.
        let mut interval_offset = Duration::from_nanos(
            (self.vsync_phase.into_nanos() - time_now.into_nanos())
                % self.vsync_interval.into_nanos(),
        );
        // Unless |time_now| is exactly on the interval, adjust forward to the next
        // vsync after |time_now|.
        if interval_offset != Duration::from_nanos(0) && self.vsync_phase < time_now {
            interval_offset += self.vsync_interval;
        }

        let (image_index, actual_image_id) = if let Some(available) = image_id {
            (
                *self.display_resources().image_indexes.get(&available).expect("image index"),
                available,
            )
        } else {
            (0, 0)
        };

        let buffer_count = self.display_resources.as_ref().and_then(|display_resources| {
            Some(display_resources.frame_collection.get_frame_count())
        });
        let display_rotation = self.display_rotation;
        let frame_buffer = self.frame_buffer.clone();
        let app_sender = self.app_sender.clone();
        let mouse_cursor_position = self.mouse_cursor_position.clone();

        ViewAssistantContext {
            key: view_details.key,
            size: match display_rotation {
                DisplayRotation::Deg0 | DisplayRotation::Deg180 => view_details.physical_size,
                DisplayRotation::Deg90 | DisplayRotation::Deg270 => {
                    size2(view_details.physical_size.height, view_details.physical_size.width)
                }
            },
            metrics: view_details.metrics,
            presentation_time: time_now + interval_offset,
            messages: Vec::new(),
            buffer_count,
            image_id: actual_image_id,
            image_index,
            frame_buffer: Some(frame_buffer),
            app_sender,
            mouse_cursor_position,
        }
    }

    async fn allocate_display_resources(
        collection_id: u64,
        use_spinel: bool,
        size: IntSize,
        frame_buffer: &FrameBufferPtr,
        pixel_format: fuchsia_framebuffer::PixelFormat,
        display_rotation: DisplayRotation,
        render_frame_count: usize,
    ) -> Result<DisplayResources, Error> {
        let usage = if use_spinel { FrameUsage::Gpu } else { FrameUsage::Cpu };
        let unsize = size.floor().to_u32();
        let mut fb = frame_buffer.borrow_mut();
        let mut frame_collection_builder = FrameCollectionBuilder::new(
            unsize.width,
            unsize.height,
            pixel_format.into(),
            usage,
            render_frame_count,
        )?;
        let context = {
            let context_token = frame_collection_builder.duplicate_token().await?;
            Context {
                inner: if use_spinel {
                    ContextInner::Spinel(generic::Spinel::new_context(
                        context_token,
                        unsize,
                        display_rotation,
                    ))
                } else {
                    ContextInner::Mold(generic::Mold::new_context(
                        context_token,
                        unsize,
                        display_rotation,
                    ))
                },
            }
        };

        let fb_pixel_format = if use_spinel { context.pixel_format() } else { pixel_format };
        let config_for_format = fb.get_config_for_format(fb_pixel_format);
        let frame_collection = frame_collection_builder
            .build(collection_id, &config_for_format, true, &mut fb)
            .await?;
        let (image_indexes, wait_events, frame_set) =
            Self::allocate_frames(fb_pixel_format, &frame_collection)
                .await
                .expect("allocate_frames");
        Ok(DisplayResources {
            context,
            image_indexes,
            wait_events,
            frame_set,
            frame_collection,
            pixel_format: fb_pixel_format,
        })
    }

    async fn maybe_reallocate_display_resources(&mut self) -> Result<(), Error> {
        if self.display_resources.is_none() {
            instant!(
                "gfx",
                "FrameBufferViewStrategy::allocate_display_resources",
                fuchsia_trace::Scope::Process,
                "" => ""
            );
            self.collection_id += 1;
            self.display_resources = Some(
                Self::allocate_display_resources(
                    self.collection_id,
                    Config::get().use_spinel,
                    self.size,
                    &self.frame_buffer,
                    self.pixel_format,
                    self.display_rotation,
                    self.render_frame_count,
                )
                .await?,
            );
        }
        Ok(())
    }

    fn display_resources(&mut self) -> &mut DisplayResources {
        self.display_resources.as_mut().expect("display_resources")
    }

    fn update_image(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        image: u64,
    ) {
        instant!(
            "gfx",
            "FrameBufferViewStrategy::update_image",
            fuchsia_trace::Scope::Process,
            "image" => format!("{}", image).as_str()
        );
        let buffer_ready_event =
            self.display_resources().wait_events.get(&image).expect("wait event");
        let buffer_ready_event =
            buffer_ready_event.duplicate_handle(zx::Rights::SAME_RIGHTS).expect("duplicate_handle");
        let framebuffer_context = self.make_context(view_details, Some(image));

        view_assistant
            .render(&mut self.display_resources().context, buffer_ready_event, &framebuffer_context)
            .unwrap_or_else(|e| panic!("Update error: {:?}", e));
    }
}

#[async_trait(?Send)]
impl ViewStrategy for FrameBufferViewStrategy {
    fn initial_metrics(&self) -> Size {
        size2(1.0, 1.0)
    }

    fn initial_physical_size(&self) -> Size {
        let config = self.frame_buffer.borrow().get_config();
        size2(config.width as f32, config.height as f32)
    }

    fn initial_logical_size(&self) -> Size {
        self.initial_physical_size()
    }

    fn setup(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        if let Some(available) = self.display_resources().frame_set.get_available_image() {
            let framebuffer_context = self.make_context(view_details, Some(available));
            view_assistant
                .setup(&framebuffer_context)
                .unwrap_or_else(|e| panic!("Setup error: {:?}", e));
            self.display_resources().frame_set.return_image(available);
        }
    }

    async fn render(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
    ) -> bool {
        duration!("gfx", "FrameBufferViewStrategy::update");
        self.maybe_reallocate_display_resources()
            .await
            .expect("maybe_reallocate_display_resources");
        if let Some(available) = self.display_resources().frame_set.get_available_image() {
            self.update_image(view_details, view_assistant, available);
            self.display_resources().frame_set.mark_prepared(available);
            true
        } else {
            if self.render_frame_count == 1 {
                if let Some(presented) = self.presented {
                    self.update_image(view_details, view_assistant, presented);
                    true
                } else {
                    false
                }
            } else {
                false
            }
        }
    }

    fn present(&mut self, _view_details: &ViewDetails) {
        duration!("gfx", "FrameBufferViewStrategy::present");
        if self.render_frame_count == 1 && self.presented.is_some() {
            return;
        }
        if let Some(prepared) = self.display_resources().frame_set.prepared {
            instant!(
                "gfx",
                "FrameBufferViewStrategy::present_image",
                fuchsia_trace::Scope::Process,
                "prepared" => format!("{}", prepared).as_str()
            );
            let mut fb = self.frame_buffer.borrow_mut();
            if let Some(display_resources) = self.display_resources.as_mut() {
                let frame = display_resources.frame_collection.get_frame(prepared);
                fb.present_frame(frame, Some(self.image_sender.clone()), false)
                    .unwrap_or_else(|e| panic!("Present error: {:?}", e));
                display_resources.frame_set.mark_presented(prepared);
                self.presented = Some(prepared);
            }
        }
    }

    fn handle_focus(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        focus: bool,
    ) {
        let mut framebuffer_context = self.make_context(view_details, None);
        view_assistant
            .handle_focus_event(&mut framebuffer_context, focus)
            .unwrap_or_else(|e| panic!("handle_focus error: {:?}", e));
    }

    fn handle_scenic_input_event(
        &mut self,
        _: &ViewDetails,
        _: &mut ViewAssistantPtr,
        _: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Vec<Message> {
        panic!("Scenic events should not be delivered when running under the frame buffer")
    }

    fn handle_scenic_key_event(
        &mut self,
        _: &ViewDetails,
        _: &mut ViewAssistantPtr,
        _: &fidl_fuchsia_ui_input3::KeyEvent,
    ) -> Vec<Message> {
        panic!("Scenic key events should not be delivered when running under the frame buffer")
    }

    fn handle_input_event(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        event: &input::Event,
    ) -> Vec<Message> {
        match &event.event_type {
            input::EventType::Mouse(mouse_event) => {
                self.mouse_cursor_position = Some(mouse_event.location);
                self.app_sender
                    .unbounded_send(MessageInternal::RequestRender(view_details.key))
                    .expect("unbounded_send");
            }
            _ => (),
        };
        let mut framebuffer_context = self.make_context(view_details, None);
        view_assistant
            .handle_input_event(&mut framebuffer_context, &event)
            .unwrap_or_else(|e| eprintln!("handle_new_input_event: {:?}", e));

        framebuffer_context.messages
    }

    fn image_freed(&mut self, image_id: u64, collection_id: u32) {
        if collection_id as u64 == self.collection_id {
            instant!(
                "gfx",
                "FrameBufferViewStrategy::image_freed",
                fuchsia_trace::Scope::Process,
                "image_freed" => format!("{}", image_id).as_str()
            );
            if let Some(display_resources) = self.display_resources.as_mut() {
                display_resources.frame_set.mark_done_presenting(image_id);
            }
        }
    }

    fn handle_vsync_parameters_changed(&mut self, phase: Time, interval: Duration) {
        self.vsync_phase = phase;
        self.vsync_interval = interval;
    }

    fn handle_vsync_cookie(&mut self, cookie: u64) {
        let mut fb = self.frame_buffer.borrow_mut();
        fb.acknowledge_vsync(cookie).expect("acknowledge_vsync");
    }

    fn ownership_changed(&mut self, owned: bool) {
        if !owned {
            let timer = fasync::Timer::new(fuchsia_async::Time::after(
                self.display_resource_release_delay.into(),
            ));
            let timer_sender = self.app_sender.clone();
            let task = fasync::Task::local(async move {
                timer.await;
                timer_sender
                    .unbounded_send(MessageInternal::DropDisplayResources)
                    .expect("unbounded_send");
            });
            self.drop_display_resources_task = Some(task);
        } else {
            self.drop_display_resources_task = None;
        }
    }

    fn drop_display_resources(&mut self) {
        let task = self.drop_display_resources_task.take();
        if task.is_some() {
            instant!(
                "gfx",
                "FrameBufferViewStrategy::drop_display_resources",
                fuchsia_trace::Scope::Process,
                "" => ""
            );
            self.display_resources = None;
            self.presented = None;
        }
    }
}
