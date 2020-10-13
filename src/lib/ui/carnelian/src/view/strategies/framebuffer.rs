// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::{
    app::{FrameBufferPtr, MessageInternal, RenderOptions},
    geometry::{IntSize, Size, UintSize},
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
use anyhow::Error;
use async_trait::async_trait;
use fidl::endpoints::create_endpoints;
use fuchsia_async::{self as fasync};
use fuchsia_framebuffer::{FrameSet, ImageId};
use fuchsia_trace::{self, duration, instant};
use fuchsia_zircon::{self as zx, Duration, Event, HandleBased, Time};
use futures::{
    channel::mpsc::{unbounded, UnboundedSender},
    StreamExt,
};
use std::collections::BTreeMap;

type WaitEvents = BTreeMap<ImageId, Event>;

pub(crate) struct FrameBufferViewStrategy {
    app_sender: UnboundedSender<MessageInternal>,
    frame_buffer: FrameBufferPtr,
    frame_set: FrameSet,
    image_indexes: BTreeMap<ImageId, u32>,
    context: Context,
    wait_events: WaitEvents,
    image_sender: futures::channel::mpsc::UnboundedSender<u64>,
    vsync_phase: Time,
    vsync_interval: Duration,
}

const RENDER_FRAME_COUNT: usize = 2;

impl FrameBufferViewStrategy {
    pub(crate) async fn new(
        key: ViewKey,
        render_options: RenderOptions,
        size: &IntSize,
        pixel_format: fuchsia_framebuffer::PixelFormat,
        app_sender: UnboundedSender<MessageInternal>,
        frame_buffer: FrameBufferPtr,
    ) -> Result<ViewStrategyPtr, Error> {
        app_sender.unbounded_send(MessageInternal::Render(key)).expect("unbounded_send");
        app_sender.unbounded_send(MessageInternal::Focus(key)).expect("unbounded_send");
        let unsize = UintSize::new(size.width as u32, size.height as u32);
        let mut fb = frame_buffer.borrow_mut();
        let context = {
            let (context_token, context_token_request) =
                create_endpoints::<fidl_fuchsia_sysmem::BufferCollectionTokenMarker>()?;

            // Duplicate local token for display.
            fb.local_token
                .as_ref()
                .expect("local_token")
                .duplicate(std::u32::MAX, context_token_request)?;

            // Ensure that duplicate message has been processed by sysmem.
            fb.local_token.as_ref().expect("local_token").sync().await?;
            Context {
                inner: if render_options.use_spinel {
                    ContextInner::Spinel(generic::Spinel::new_context(context_token, unsize))
                } else {
                    ContextInner::Mold(generic::Mold::new_context(context_token, unsize))
                },
            }
        };

        let image_freed_sender = app_sender.clone();
        let (image_sender, mut image_receiver) = unbounded::<u64>();
        // wait for events from the image freed fence to know when an
        // image can prepared.
        fasync::Task::local(async move {
            while let Some(image_id) = image_receiver.next().await {
                image_freed_sender
                    .unbounded_send(MessageInternal::ImageFreed(key, image_id, 0))
                    .expect("unbounded_send");
            }
        })
        .detach();
        let fb_pixel_format =
            if render_options.use_spinel { context.pixel_format() } else { pixel_format };
        fb.allocate_frames(RENDER_FRAME_COUNT, fb_pixel_format).await?;
        let fb_image_ids = fb.get_image_ids();
        let mut image_indexes = BTreeMap::new();
        let mut wait_events: WaitEvents = WaitEvents::new();
        for frame_image_id in &fb_image_ids {
            let frame = fb.get_frame(*frame_image_id);
            let frame_index = frame.image_index;
            image_indexes.insert(*frame_image_id, frame_index);
            let wait_event = frame
                .wait_event
                .duplicate_handle(zx::Rights::SAME_RIGHTS)
                .expect("duplicate_handle");
            wait_events.insert(frame.image_id, wait_event);
        }
        let frame_set = FrameSet::new(fb_image_ids);
        Ok(Box::new(FrameBufferViewStrategy {
            app_sender,
            frame_buffer: frame_buffer.clone(),
            frame_set: frame_set,
            image_indexes,
            context,
            wait_events,
            image_sender: image_sender,
            vsync_phase: Time::get_monotonic(),
            vsync_interval: Duration::from_millis(16),
        }))
    }

    fn make_context(
        &mut self,
        view_details: &ViewDetails,
        image_id: Option<ImageId>,
    ) -> (ViewAssistantContext, &mut Context) {
        let render_context = &mut self.context;

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
            (*self.image_indexes.get(&available).expect("image index"), available)
        } else {
            (0, 0)
        };

        (
            ViewAssistantContext {
                key: view_details.key,
                size: view_details.physical_size,
                metrics: view_details.metrics,
                presentation_time: time_now + interval_offset,
                messages: Vec::new(),
                buffer_count: Some(self.frame_buffer.borrow().get_frame_count()),
                image_id: actual_image_id,
                image_index,
                frame_buffer: Some(self.frame_buffer.clone()),
                app_sender: self.app_sender.clone(),
            },
            render_context,
        )
    }
}

#[async_trait(?Send)]
impl ViewStrategy for FrameBufferViewStrategy {
    fn initial_metrics(&self) -> Size {
        Size::new(1.0, 1.0)
    }

    fn initial_physical_size(&self) -> Size {
        let config = self.frame_buffer.borrow().get_config();
        Size::new(config.width as f32, config.height as f32)
    }

    fn initial_logical_size(&self) -> Size {
        self.initial_physical_size()
    }

    fn setup(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        if let Some(available) = self.frame_set.get_available_image() {
            let (framebuffer_context, ..) = self.make_context(view_details, Some(available));
            view_assistant
                .setup(&framebuffer_context)
                .unwrap_or_else(|e| panic!("Setup error: {:?}", e));
            self.frame_set.return_image(available);
        }
    }

    async fn render(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
    ) -> bool {
        duration!("gfx", "FrameBufferViewStrategy::update");
        if let Some(available) = self.frame_set.get_available_image() {
            instant!(
                "gfx",
                "FrameBufferViewStrategy::update_image",
                fuchsia_trace::Scope::Process,
                "available" => format!("{}", available).as_str()
            );
            let buffer_ready_event = self.wait_events.get(&available).expect("wait event");
            let buffer_ready_event = buffer_ready_event
                .duplicate_handle(zx::Rights::SAME_RIGHTS)
                .expect("duplicate_handle");
            let (framebuffer_context, render_context) =
                self.make_context(view_details, Some(available));
            view_assistant
                .render(render_context, buffer_ready_event, &framebuffer_context)
                .unwrap_or_else(|e| panic!("Update error: {:?}", e));
            self.frame_set.mark_prepared(available);
            true
        } else {
            false
        }
    }

    fn present(&mut self, _view_details: &ViewDetails) {
        duration!("gfx", "FrameBufferViewStrategy::present");
        if let Some(prepared) = self.frame_set.prepared {
            instant!(
                "gfx",
                "FrameBufferViewStrategy::present_image",
                fuchsia_trace::Scope::Process,
                "prepared" => format!("{}", prepared).as_str()
            );
            let mut fb = self.frame_buffer.borrow_mut();
            fb.present_frame(prepared, Some(self.image_sender.clone()), false)
                .unwrap_or_else(|e| panic!("Present error: {:?}", e));
            self.frame_set.mark_presented(prepared);
        }
    }

    fn handle_focus(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        focus: bool,
    ) {
        let (mut framebuffer_context, ..) = self.make_context(view_details, None);
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

    fn handle_input_event(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        event: &input::Event,
    ) -> Vec<Message> {
        let (mut framebuffer_context, _render_context) = self.make_context(view_details, None);
        view_assistant
            .handle_input_event(&mut framebuffer_context, &event)
            .unwrap_or_else(|e| eprintln!("handle_new_input_event: {:?}", e));

        framebuffer_context.messages
    }

    fn image_freed(&mut self, image_id: u64, _collection_id: u32) {
        instant!(
            "gfx",
            "FrameBufferViewStrategy::image_freed",
            fuchsia_trace::Scope::Process,
            "image_freed" => format!("{}", image_id).as_str()
        );
        self.frame_set.mark_done_presenting(image_id);
    }

    fn handle_vsync_parameters_changed(&mut self, phase: Time, interval: Duration) {
        self.vsync_phase = phase;
        self.vsync_interval = interval;
    }

    fn handle_vsync_cookie(&mut self, cookie: u64) {
        let mut fb = self.frame_buffer.borrow_mut();
        fb.acknowledge_vsync(cookie).expect("acknowledge_vsync");
    }
}
