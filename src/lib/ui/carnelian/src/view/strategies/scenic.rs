// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::{
    app::{Config, MessageInternal},
    drawing::DisplayRotation,
    geometry::UintSize,
    input::scenic::ScenicInputHandler,
    message::Message,
    render::{
        self,
        generic::{self, Backend},
        ContextInner,
    },
    view::{
        strategies::base::{ViewStrategy, ViewStrategyPtr},
        View, ViewAssistantContext, ViewAssistantPtr, ViewDetails, ViewKey,
    },
};
use anyhow::{Context, Error, Result};
use async_trait::async_trait;
use fidl::endpoints::create_request_stream;
use fidl_fuchsia_ui_gfx::{self as gfx};
use fidl_fuchsia_ui_views::{ViewRef, ViewRefControl, ViewToken};
use fuchsia_async::{self as fasync, OnSignals};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_framebuffer::{sysmem::BufferCollectionAllocator, FrameSet, FrameUsage, ImageId};
use fuchsia_scenic::{EntityNode, Image2, Material, Rectangle, SessionPtr, ShapeNode};
use fuchsia_trace::{self, duration, instant};
use fuchsia_zircon::{self as zx, Event, HandleBased, Signals, Time};
use futures::{channel::mpsc::UnboundedSender, TryStreamExt};
use std::collections::{BTreeMap, BTreeSet};

struct Plumber {
    pub size: UintSize,
    // TODO(fxbug.dev/77068): Remove this attribute.
    #[allow(dead_code)]
    pub buffer_count: usize,
    pub collection_id: u32,
    // TODO(fxbug.dev/77068): Remove this attribute.
    #[allow(dead_code)]
    pub first_image_id: u64,
    pub frame_set: FrameSet,
    pub image_indexes: BTreeMap<ImageId, u32>,
    pub images: BTreeMap<ImageId, Image2>,
    pub context: render::Context,
}

impl Plumber {
    async fn new(
        session: &SessionPtr,
        size: UintSize,
        pixel_format: fidl_fuchsia_sysmem::PixelFormatType,
        buffer_count: usize,
        collection_id: u32,
        first_image_id: u64,
    ) -> Result<Plumber, Error> {
        let use_spinel = Config::get().use_spinel;
        let usage = if use_spinel { FrameUsage::Gpu } else { FrameUsage::Cpu };
        let mut buffer_allocator = BufferCollectionAllocator::new(
            size.width,
            size.height,
            pixel_format,
            usage,
            buffer_count,
        )?;

        buffer_allocator.set_name(100, "CarnelianSurface")?;

        let buffer_collection_token = buffer_allocator.duplicate_token().await?;
        session.lock().register_buffer_collection(collection_id, buffer_collection_token)?;

        let context_token = buffer_allocator.duplicate_token().await?;
        let context = render::Context {
            inner: if use_spinel {
                ContextInner::Spinel(generic::Spinel::new_context(
                    context_token,
                    size,
                    DisplayRotation::Deg0,
                ))
            } else {
                ContextInner::Mold(generic::Mold::new_context(
                    context_token,
                    size,
                    DisplayRotation::Deg0,
                ))
            },
        };
        let buffers = buffer_allocator.allocate_buffers(true).await.context("allocate_buffers")?;

        let mut image_ids = BTreeSet::new();
        let mut image_indexes = BTreeMap::new();
        let mut images = BTreeMap::new();
        for index in 0..buffers.buffer_count as usize {
            let image_id = index + first_image_id as usize;
            image_ids.insert(image_id as u64);
            let uindex = index as u32;
            image_indexes.insert(image_id as u64, uindex);
            let image = Image2::new(session, size.width, size.height, collection_id, uindex);
            images.insert(image_id as u64, image);
        }

        let frame_set = FrameSet::new(collection_id as u64, image_ids);
        Ok(Plumber {
            size,
            buffer_count,
            collection_id,
            first_image_id,
            frame_set,
            image_indexes,
            images,
            context,
        })
    }

    pub fn enter_retirement(&mut self, session: &SessionPtr) {
        session
            .lock()
            .deregister_buffer_collection(self.collection_id)
            .expect("deregister_buffer_collection");
    }
}

const RENDER_BUFFER_COUNT: usize = 3;
const DEFAULT_PRESENT_INTERVAL: i64 = (1_000_000_000.0 / 60.0) as i64;

#[derive(Clone, Copy)]
struct PresentationTime {
    presentation_time: i64,
    latch_point: i64,
}

pub(crate) struct ScenicViewStrategy {
    #[allow(unused)]
    view: View,
    #[allow(unused)]
    root_node: EntityNode,
    session: SessionPtr,
    view_key: ViewKey,
    pending_present_count: usize,
    last_presentation_time: i64,
    future_presentation_times: Vec<PresentationTime>,
    custom_render_offset: Option<i64>,
    present_interval: i64,
    num_presents_allowed: usize,
    render_timer_scheduled: bool,
    missed_frame: bool,
    app_sender: UnboundedSender<MessageInternal>,
    content_node: ShapeNode,
    content_material: Material,
    next_buffer_collection: u32,
    plumber: Option<Plumber>,
    retiring_plumbers: Vec<Plumber>,
    next_image_id: u64,
    input_handler: ScenicInputHandler,
}

impl ScenicViewStrategy {
    pub(crate) async fn new(
        key: ViewKey,
        session: &SessionPtr,
        view_token: ViewToken,
        control_ref: ViewRefControl,
        view_ref: ViewRef,
        app_sender: UnboundedSender<MessageInternal>,
    ) -> Result<ViewStrategyPtr, Error> {
        let view_ref_key = fuchsia_scenic::duplicate_view_ref(&view_ref)?;
        let (view, root_node, content_material, content_node) =
            Self::create_scenic_resources(session, view_token, control_ref, view_ref);
        Self::listen_for_session_events(session, &app_sender, key);
        Self::listen_for_key_events(view_ref_key, &app_sender, key)?;

        // This initial present is needed in order for session events to start flowing.
        Self::do_present(session, &app_sender, key, 0);

        let strat = ScenicViewStrategy {
            view,
            session: session.clone(),
            view_key: key,
            app_sender: app_sender.clone(),
            pending_present_count: 1,
            last_presentation_time: fasync::Time::now().into_nanos(),
            future_presentation_times: Vec::new(),
            custom_render_offset: None,
            present_interval: DEFAULT_PRESENT_INTERVAL,
            num_presents_allowed: 1,
            render_timer_scheduled: false,
            missed_frame: false,
            root_node,
            content_node,
            content_material,
            plumber: None,
            retiring_plumbers: Vec::new(),
            next_buffer_collection: 1,
            next_image_id: 1,
            input_handler: ScenicInputHandler::new(),
        };

        Ok(Box::new(strat))
    }

    fn create_scenic_resources(
        session: &SessionPtr,
        view_token: ViewToken,
        control_ref: ViewRefControl,
        view_ref: ViewRef,
    ) -> (View, EntityNode, Material, ShapeNode) {
        let content_material = Material::new(session.clone());
        let content_node = ShapeNode::new(session.clone());
        content_node.set_material(&content_material);
        session.lock().flush();
        let view = View::new3(
            session.clone(),
            view_token,
            control_ref,
            view_ref,
            Some(String::from("Carnelian View")),
        );
        let root_node = EntityNode::new(session.clone());
        root_node.add_child(&content_node);
        root_node.resource().set_event_mask(gfx::METRICS_EVENT_MASK);
        view.add_child(&root_node);

        (view, root_node, content_material, content_node)
    }

    fn listen_for_session_events(
        session: &SessionPtr,
        app_sender: &UnboundedSender<MessageInternal>,
        key: ViewKey,
    ) {
        let event_sender = app_sender.clone();
        let mut event_stream = session.lock().take_event_stream();
        fasync::Task::local(async move {
            while let Some(event) = event_stream.try_next().await.expect("Failed to get next event")
            {
                match event {
                    fidl_fuchsia_ui_scenic::SessionEvent::OnFramePresented {
                        frame_presented_info,
                    } => {
                        event_sender
                            .unbounded_send(MessageInternal::ScenicPresentDone(
                                key,
                                frame_presented_info,
                            ))
                            .expect("unbounded_send");
                    }
                }
            }
        })
        .detach();
    }

    fn listen_for_key_events(
        mut view_ref: ViewRef,
        app_sender: &UnboundedSender<MessageInternal>,
        key: ViewKey,
    ) -> Result<(), Error> {
        let keyboard = connect_to_protocol::<fidl_fuchsia_ui_input3::KeyboardMarker>()
            .context("Failed to connect to Keyboard service")?;

        let (listener_client_end, mut listener_stream) =
            create_request_stream::<fidl_fuchsia_ui_input3::KeyboardListenerMarker>()?;

        let event_sender = app_sender.clone();

        fasync::Task::local(async move {
            keyboard.add_listener(&mut view_ref, listener_client_end).await.expect("add_listener");

            while let Some(event) =
                listener_stream.try_next().await.expect("Failed to get next key event")
            {
                match event {
                    fidl_fuchsia_ui_input3::KeyboardListenerRequest::OnKeyEvent {
                        event,
                        responder,
                        ..
                    } => {
                        // Carnelian always considers key events handled. In the future, there
                        // might be a use case that requires letting view assistants change
                        // this behavior but none currently exists.
                        responder
                            .send(fidl_fuchsia_ui_input3::KeyEventStatus::Handled)
                            .expect("send");
                        event_sender
                            .unbounded_send(MessageInternal::ScenicKeyEvent(key, event))
                            .expect("unbounded_send");
                    }
                }
            }
        })
        .detach();
        Ok(())
    }

    fn do_present(
        session: &SessionPtr,
        app_sender: &UnboundedSender<MessageInternal>,
        key: ViewKey,
        presentation_time: i64,
    ) {
        let present_sender = app_sender.clone();
        let info_fut = session.lock().present2(presentation_time, 0);
        fasync::Task::local(async move {
            match info_fut.await {
                // TODO: figure out how to recover from this error
                Err(err) => eprintln!("Present Error: {}", err),
                Ok(info) => {
                    present_sender
                        .unbounded_send(MessageInternal::ScenicPresentSubmitted(key, info))
                        .expect("unbounded_send");
                }
            }
        })
        .detach();
    }

    fn make_view_assistant_context_with_time(
        view_details: &ViewDetails,
        image_id: ImageId,
        image_index: u32,
        app_sender: UnboundedSender<MessageInternal>,
        presentation_time: Time,
    ) -> ViewAssistantContext {
        ViewAssistantContext {
            key: view_details.key,
            size: view_details.logical_size,
            metrics: view_details.metrics,
            presentation_time,
            messages: Vec::new(),
            buffer_count: None,
            image_id,
            image_index,
            frame_buffer: None,
            app_sender,
            mouse_cursor_position: None,
        }
    }

    fn make_view_assistant_context(
        view_details: &ViewDetails,
        image_id: ImageId,
        image_index: u32,
        app_sender: UnboundedSender<MessageInternal>,
    ) -> ViewAssistantContext {
        Self::make_view_assistant_context_with_time(
            view_details,
            image_id,
            image_index,
            app_sender,
            Time::get_monotonic(),
        )
    }

    async fn create_plumber(&mut self, size: UintSize) -> Result<(), Error> {
        let buffer_collection_id = self.next_buffer_collection;
        self.next_buffer_collection = self.next_buffer_collection.wrapping_add(1);
        let next_image_id = self.next_image_id;
        self.next_image_id = self.next_image_id.wrapping_add(RENDER_BUFFER_COUNT as u64);
        self.plumber = Some(
            Plumber::new(
                &self.session,
                size.to_u32(),
                fidl_fuchsia_sysmem::PixelFormatType::Bgra32,
                RENDER_BUFFER_COUNT,
                buffer_collection_id,
                next_image_id,
            )
            .await
            .expect("VmoPlumber::new"),
        );
        Ok(())
    }

    fn next_presentation_time(&self) -> PresentationTime {
        let now = fasync::Time::now().into_nanos();
        let legal_next = PresentationTime {
            presentation_time: self.last_presentation_time + self.present_interval,
            latch_point: now,
        };
        // Find a future presentation time that is at least half an interval past
        // the last presentation time and has a latch point that is in the future.
        let earliest_presentation_time = self.last_presentation_time + self.present_interval / 2;
        let next = self
            .future_presentation_times
            .iter()
            .find(|t| t.presentation_time >= earliest_presentation_time && t.latch_point > now)
            .unwrap_or(&legal_next);
        *next
    }

    fn schedule_render_timer(&mut self) {
        if !self.render_timer_scheduled && !self.missed_frame {
            let presentation_time = self.next_presentation_time();
            const DEFAULT_RENDER_OFFSET_DELTA: i64 = 1_000_000; // 1ms
            let render_offset = self
                .custom_render_offset
                .unwrap_or_else(|| self.present_interval - DEFAULT_RENDER_OFFSET_DELTA);
            let render_time = presentation_time.latch_point - render_offset;
            let timer = fasync::Timer::new(fuchsia_async::Time::from_nanos(render_time));
            let timer_sender = self.app_sender.clone();
            let key = self.view_key;
            fasync::Task::local(async move {
                timer.await;
                timer_sender.unbounded_send(MessageInternal::Render(key)).expect("unbounded_send");
            })
            .detach();
            self.render_timer_scheduled = true;
        }
    }

    fn adjust_scenic_resources(&mut self, view_details: &ViewDetails) {
        let center_x = view_details.physical_size.width * 0.5;
        let center_y = view_details.physical_size.height * 0.5;
        self.content_node.set_translation(center_x, center_y, -0.1);
        let rectangle = Rectangle::new(
            self.session.clone(),
            view_details.physical_size.width as f32,
            view_details.physical_size.height as f32,
        );
        self.content_node.set_shape(&rectangle);
    }

    fn render_to_image_from_plumber(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
    ) -> bool {
        let presentation_time = self.next_presentation_time();
        let plumber = self.plumber.as_mut().expect("plumber");
        if let Some(available) = plumber.frame_set.get_available_image() {
            duration!("gfx", "ScenicViewStrategy::render.render_to_image");
            let available_index = plumber.image_indexes.get(&available).expect("index for image");
            let image2 = plumber.images.get(&available).expect("image2");
            self.content_material.set_texture_resource(Some(&image2));
            let render_context = ScenicViewStrategy::make_view_assistant_context_with_time(
                view_details,
                available,
                *available_index,
                self.app_sender.clone(),
                Time::from_nanos(presentation_time.presentation_time),
            );
            let buffer_ready_event = Event::create().expect("Event.create");
            view_assistant
                .render(&mut plumber.context, buffer_ready_event, &render_context)
                .unwrap_or_else(|e| panic!("Update error: {:?}", e));
            plumber.frame_set.mark_prepared(available);
            let key = view_details.key;
            let collection_id = plumber.collection_id;
            let done_event = Event::create().expect("Event.create");
            let local_done_event =
                done_event.duplicate_handle(zx::Rights::SAME_RIGHTS).expect("duplicate_handle");
            self.session.lock().add_release_fence(done_event);
            let app_sender = self.app_sender.clone();
            fasync::Task::local(async move {
                let signals = OnSignals::new(&local_done_event, Signals::EVENT_SIGNALED);
                signals.await.expect("to wait");
                app_sender
                    .unbounded_send(MessageInternal::ImageFreed(key, available, collection_id))
                    .expect("unbounded_send");
            })
            .detach();

            // Image is guaranteed to be presented at this point.
            plumber.frame_set.mark_presented(available);
            true
        } else {
            instant!(
                "gfx",
                "ScenicViewStrategy::no_available_image",
                fuchsia_trace::Scope::Process
            );
            self.missed_frame = true;
            false
        }
    }

    fn retry_missed_frame(&mut self) {
        if self.missed_frame {
            self.missed_frame = false;
            self.render_requested();
        }
    }

    fn present_allowed(&self) -> bool {
        self.pending_present_count < self.num_presents_allowed
    }
}

#[async_trait(?Send)]
impl ViewStrategy for ScenicViewStrategy {
    fn setup(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        let render_context = ScenicViewStrategy::make_view_assistant_context(
            view_details,
            0,
            0,
            self.app_sender.clone(),
        );
        view_assistant.setup(&render_context).unwrap_or_else(|e| panic!("Setup error: {:?}", e));
        self.custom_render_offset = view_assistant.get_render_offset();
    }

    async fn render(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
    ) -> bool {
        duration!("gfx", "ScenicViewStrategy::render");
        self.render_timer_scheduled = false;
        let size = view_details.logical_size.floor().to_u32();
        if size.width > 0 && size.height > 0 {
            if !self.present_allowed() {
                instant!(
                    "gfx",
                    "ScenicViewStrategy::present_is_not_allowed",
                    fuchsia_trace::Scope::Process,
                    "counts" => format!("{} pending {} allowed", self.pending_present_count, self.num_presents_allowed).as_str()
                );
                self.missed_frame = true;
                return false;
            }

            self.adjust_scenic_resources(view_details);

            if self.plumber.is_none() {
                duration!("gfx", "ScenicViewStrategy::render.create_plumber");
                self.create_plumber(size).await.expect("create_plumber");
            } else {
                let current_size = self.plumber.as_ref().expect("plumber").size;
                if current_size != size {
                    duration!("gfx", "ScenicViewStrategy::render.create_plumber");
                    let retired_plumber = self.plumber.take().expect("plumber");
                    self.retiring_plumbers.push(retired_plumber);
                    self.create_plumber(size).await.expect("create_plumber");
                }
            }
            self.render_to_image_from_plumber(view_details, view_assistant)
        } else {
            true
        }
    }

    fn present(&mut self, _view_details: &ViewDetails) {
        duration!("gfx", "ScenicViewStrategy::present");
        if !self.missed_frame {
            let presentation_time = self.next_presentation_time();
            Self::do_present(
                &self.session,
                &self.app_sender,
                self.view_key,
                presentation_time.presentation_time,
            );
            self.last_presentation_time = presentation_time.presentation_time;
            self.pending_present_count += 1;
        }
    }

    fn present_submitted(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
        info: fidl_fuchsia_scenic_scheduling::FuturePresentationTimes,
    ) {
        let present_intervals = info.future_presentations.len();
        if present_intervals > 0 {
            let times: Vec<_> = info
                .future_presentations
                .iter()
                .filter_map(|info| info.presentation_time)
                .collect();
            let average_interval: i64 =
                times.as_slice().windows(2).map(|slice| slice[1] - slice[0]).sum::<i64>()
                    / present_intervals as i64;
            self.present_interval = average_interval;
        } else {
            self.present_interval = DEFAULT_PRESENT_INTERVAL;
        }
        self.future_presentation_times.splice(
            ..,
            info.future_presentations.iter().map(|t| PresentationTime {
                presentation_time: t.presentation_time.expect("presentation_time"),
                latch_point: t.latch_point.expect("latch_point"),
            }),
        );
    }

    fn present_done(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
        info: fidl_fuchsia_scenic_scheduling::FramePresentedInfo,
    ) {
        let num_presents_handled = info.presentation_infos.len();
        assert!(self.pending_present_count >= num_presents_handled);
        self.pending_present_count -= num_presents_handled;
        self.num_presents_allowed = info.num_presents_allowed as usize;
        self.retry_missed_frame();
        instant!(
            "gfx",
            "ScenicViewStrategy::present_done",
            fuchsia_trace::Scope::Process,
            "presents" => format!("{} handled {} allowed", num_presents_handled, info.num_presents_allowed).as_str()
        );
    }

    fn handle_focus(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        focus: bool,
    ) {
        let mut render_context = ScenicViewStrategy::make_view_assistant_context(
            view_details,
            0,
            0,
            self.app_sender.clone(),
        );
        view_assistant
            .handle_focus_event(&mut render_context, focus)
            .unwrap_or_else(|e| panic!("handle_focus error: {:?}", e));
    }

    fn handle_scenic_input_event(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        event: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Vec<Message> {
        let events = self.input_handler.handle_scenic_input_event(&view_details.metrics, &event);

        let mut render_context = ScenicViewStrategy::make_view_assistant_context(
            view_details,
            0,
            0,
            self.app_sender.clone(),
        );
        for input_event in events {
            view_assistant
                .handle_input_event(&mut render_context, &input_event)
                .unwrap_or_else(|e| eprintln!("handle_event: {:?}", e));
        }

        render_context.messages
    }

    fn handle_scenic_key_event(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        event: &fidl_fuchsia_ui_input3::KeyEvent,
    ) -> Vec<Message> {
        let events = self.input_handler.handle_scenic_key_event(&event);

        let mut render_context = ScenicViewStrategy::make_view_assistant_context(
            view_details,
            0,
            0,
            self.app_sender.clone(),
        );
        for input_event in events {
            view_assistant
                .handle_input_event(&mut render_context, &input_event)
                .unwrap_or_else(|e| eprintln!("handle_event: {:?}", e));
        }

        render_context.messages
    }

    fn image_freed(&mut self, image_id: u64, collection_id: u32) {
        instant!(
            "gfx",
            "ScenicViewStrategy::image_freed",
            fuchsia_trace::Scope::Process,
            "image_freed" => format!("{} in {}", image_id, collection_id).as_str()
        );

        self.retry_missed_frame();

        if let Some(plumber) = self.plumber.as_mut() {
            if plumber.collection_id == collection_id {
                plumber.frame_set.mark_done_presenting(image_id);
                return;
            }
        }

        for retired_plumber in &mut self.retiring_plumbers {
            if retired_plumber.collection_id == collection_id {
                retired_plumber.frame_set.mark_done_presenting(image_id);
                if retired_plumber.frame_set.no_images_in_use() {
                    retired_plumber.enter_retirement(&self.session);
                }
            }
        }

        self.retiring_plumbers.retain(|plumber| !plumber.frame_set.no_images_in_use());
    }

    fn render_requested(&mut self) {
        self.schedule_render_timer();
    }
}
