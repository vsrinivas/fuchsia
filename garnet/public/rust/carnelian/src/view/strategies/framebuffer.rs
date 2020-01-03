// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::{
    app::{FrameBufferPtr, MessageInternal},
    canvas::{Canvas, MappingPixelSink},
    geometry::IntSize,
    message::Message,
    view::{
        strategies::base::{ViewStrategy, ViewStrategyPtr},
        Canvases, ViewAssistantContext, ViewAssistantPtr, ViewDetails, ViewKey,
    },
};
use async_trait::async_trait;
use fuchsia_async::{self as fasync};
use fuchsia_framebuffer::{FrameSet, ImageId};
use fuchsia_zircon::{self as zx, ClockId, Event, HandleBased, Time};
use futures::{
    channel::mpsc::{unbounded, UnboundedSender},
    StreamExt,
};
use std::{cell::RefCell, collections::BTreeMap};

type WaitEvents = BTreeMap<ImageId, Event>;

pub(crate) struct FrameBufferViewStrategy {
    frame_buffer: FrameBufferPtr,
    canvases: Canvases,
    frame_set: FrameSet,
    image_sender: futures::channel::mpsc::UnboundedSender<u64>,
    wait_events: WaitEvents,
    signals_wait_event: bool,
}

impl FrameBufferViewStrategy {
    pub(crate) fn new(
        key: ViewKey,
        size: &IntSize,
        pixel_size: u32,
        _pixel_format: fuchsia_framebuffer::PixelFormat,
        stride: u32,
        app_sender: UnboundedSender<MessageInternal>,
        frame_buffer: FrameBufferPtr,
        signals_wait_event: bool,
    ) -> ViewStrategyPtr {
        let mut fb = frame_buffer.borrow_mut();
        let mut canvases: Canvases = Canvases::new();
        let mut wait_events: WaitEvents = WaitEvents::new();
        let image_ids = fb.get_image_ids();
        image_ids.iter().for_each(|image_id| {
            let frame = fb.get_frame_mut(*image_id);
            let canvas = RefCell::new(Canvas::new(
                *size,
                MappingPixelSink::new(&frame.mapping),
                stride,
                pixel_size,
                frame.image_id,
                frame.image_index,
            ));
            canvases.insert(frame.image_id, canvas);
            if signals_wait_event {
                let wait_event = frame
                    .wait_event
                    .duplicate_handle(zx::Rights::SAME_RIGHTS)
                    .expect("duplicate_handle");
                wait_events.insert(frame.image_id, wait_event);
            }
        });
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
        let frame_set = FrameSet::new(image_ids);
        Box::new(FrameBufferViewStrategy {
            canvases,
            frame_buffer: frame_buffer.clone(),
            frame_set: frame_set,
            image_sender: image_sender,
            wait_events,
            signals_wait_event,
        })
    }

    fn make_context(
        &mut self,
        view_details: &ViewDetails,
        image_id: ImageId,
    ) -> ViewAssistantContext<'_> {
        let wait_event = if self.signals_wait_event {
            let stored_wait_event = self.wait_events.get(&image_id).expect("wait event");
            Some(stored_wait_event)
        } else {
            None
        };

        ViewAssistantContext {
            key: view_details.key,
            logical_size: view_details.logical_size,
            size: view_details.physical_size,
            metrics: view_details.metrics,
            presentation_time: Time::get(ClockId::Monotonic),
            messages: Vec::new(),
            scenic_resources: None,
            canvas: Some(
                &self.canvases.get(&image_id).expect("failed to get canvas in make_context"),
            ),
            buffer_count: Some(self.frame_buffer.borrow().get_frame_count()),
            wait_event: wait_event,
        }
    }
}

#[async_trait(?Send)]
impl ViewStrategy for FrameBufferViewStrategy {
    fn setup(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        if let Some(available) = self.frame_set.get_available_image() {
            let framebuffer_context = self.make_context(view_details, available);
            view_assistant
                .setup(&framebuffer_context)
                .unwrap_or_else(|e| panic!("Setup error: {:?}", e));
            self.frame_set.return_image(available);
        }
    }

    async fn update(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        if let Some(available) = self.frame_set.get_available_image() {
            let framebuffer_context = self.make_context(view_details, available);
            view_assistant
                .update(&framebuffer_context)
                .unwrap_or_else(|e| panic!("Update error: {:?}", e));
            self.frame_set.mark_prepared(available);
        }
    }

    fn present(&mut self, _view_details: &ViewDetails) {
        if let Some(prepared) = self.frame_set.prepared {
            let mut fb = self.frame_buffer.borrow_mut();
            fb.present_frame(prepared, Some(self.image_sender.clone()), !self.signals_wait_event)
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
}
