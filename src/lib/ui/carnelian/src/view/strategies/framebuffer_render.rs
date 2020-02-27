// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::{
    app::{FrameBufferPtr, MessageInternal, FRAME_COUNT},
    geometry::{IntSize, UintSize},
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
use fuchsia_zircon::{self as zx, ClockId, Duration, Event, HandleBased, Time};
use futures::{
    channel::mpsc::{unbounded, UnboundedSender},
    StreamExt,
};
use std::collections::BTreeMap;

type WaitEvents = BTreeMap<ImageId, Event>;

pub(crate) struct FrameBufferRenderViewStrategy {
    frame_buffer: FrameBufferPtr,
    frame_set: FrameSet,
    image_indexes: BTreeMap<ImageId, u32>,
    context: Context,
    wait_events: WaitEvents,
    image_sender: futures::channel::mpsc::UnboundedSender<u64>,
    vsync_phase: Time,
    vsync_interval: Duration,
}

const RENDER_FRAME_COUNT: usize = FRAME_COUNT;

impl FrameBufferRenderViewStrategy {
    pub(crate) async fn new(
        key: ViewKey,
        use_mold: bool,
        size: &IntSize,
        pixel_format: fuchsia_framebuffer::PixelFormat,
        app_sender: UnboundedSender<MessageInternal>,
        frame_buffer: FrameBufferPtr,
    ) -> Result<ViewStrategyPtr, Error> {
        let unsize = UintSize::new(size.width as u32, size.height as u32);
        let mut fb = frame_buffer.borrow_mut();
        let fb_pixel_format =
            if use_mold { pixel_format } else { fuchsia_framebuffer::PixelFormat::BgrX888 };
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
                inner: if use_mold {
                    ContextInner::Mold(generic::Mold::new_context(context_token, unsize))
                } else {
                    ContextInner::Spinel(generic::Spinel::new_context(context_token, unsize))
                },
            }
        };
        let (image_sender, mut image_receiver) = unbounded::<u64>();
        // wait for events from the image freed fence to know when an
        // image can prepared.
        fasync::spawn_local(async move {
            while let Some(image_id) = image_receiver.next().await {
                app_sender
                    .unbounded_send(MessageInternal::ImageFreed(key, image_id, 0))
                    .expect("unbounded_send");
            }
        });
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
        Ok(Box::new(FrameBufferRenderViewStrategy {
            frame_buffer: frame_buffer.clone(),
            frame_set: frame_set,
            image_indexes,
            context,
            wait_events,
            image_sender: image_sender,
            vsync_phase: Time::get(ClockId::Monotonic),
            vsync_interval: Duration::from_millis(16),
        }))
    }

    fn make_context(
        &mut self,
        view_details: &ViewDetails,
        image_id: ImageId,
    ) -> (ViewAssistantContext<'_>, &mut Context) {
        let render_context = &mut self.context;

        let time_now = Time::get(ClockId::Monotonic);
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

        let image_index = *self.image_indexes.get(&image_id).expect("image index");

        (
            ViewAssistantContext {
                key: view_details.key,
                logical_size: view_details.logical_size,
                size: view_details.physical_size,
                metrics: view_details.metrics,
                presentation_time: time_now + interval_offset,
                messages: Vec::new(),
                scenic_resources: None,
                canvas: None,
                buffer_count: Some(self.frame_buffer.borrow().get_frame_count()),
                wait_event: None,
                image_id,
                image_index,
            },
            render_context,
        )
    }
}

#[async_trait(?Send)]
impl ViewStrategy for FrameBufferRenderViewStrategy {
    fn setup(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        if let Some(available) = self.frame_set.get_available_image() {
            let (framebuffer_context, ..) = self.make_context(view_details, available);
            view_assistant
                .setup(&framebuffer_context)
                .unwrap_or_else(|e| panic!("Setup error: {:?}", e));
            self.frame_set.return_image(available);
        }
    }

    async fn update(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        if let Some(available) = self.frame_set.get_available_image() {
            let buffer_ready_event = self.wait_events.get(&available).expect("wait event");
            let buffer_ready_event = buffer_ready_event
                .duplicate_handle(zx::Rights::SAME_RIGHTS)
                .expect("duplicate_handle");
            let (framebuffer_context, render_context) = self.make_context(view_details, available);
            view_assistant
                .render(render_context, buffer_ready_event, &framebuffer_context)
                .unwrap_or_else(|e| panic!("Update error: {:?}", e));
            self.frame_set.mark_prepared(available);
        }
    }

    fn present(&mut self, _view_details: &ViewDetails) {
        if let Some(prepared) = self.frame_set.prepared {
            let mut fb = self.frame_buffer.borrow_mut();
            fb.present_frame(prepared, Some(self.image_sender.clone()), false)
                .unwrap_or_else(|e| panic!("Present error: {:?}", e));
            self.frame_set.mark_presented(prepared);
        }
    }

    fn handle_input_event(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
        _event: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Vec<Message> {
        panic!("Not yet implemented");
    }

    fn image_freed(&mut self, image_id: u64, _collection_id: u32) {
        self.frame_set.mark_done_presenting(image_id);
    }

    fn handle_vsync_parameters_changed(&mut self, phase: Time, interval: Duration) {
        self.vsync_phase = phase;
        self.vsync_interval = interval;
    }
}
