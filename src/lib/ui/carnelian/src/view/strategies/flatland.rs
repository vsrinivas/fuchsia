// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::{
    app::{Config, MessageInternal},
    drawing::DisplayRotation,
    geometry::UintSize,
    input::{
        flatland::{FlatlandMouseInputHandler, FlatlandTouchInputHandler},
        key3::KeyboardInputHandler,
    },
    render::{
        self,
        generic::{self, Backend},
        ContextInner,
    },
    view::{
        strategies::base::{FlatlandParams, ViewStrategy, ViewStrategyPtr},
        UserInputMessage, ViewAssistantContext, ViewAssistantPtr, ViewDetails, ViewKey,
    },
    Size,
};
use anyhow::{bail, ensure, Context, Error, Result};
use async_trait::async_trait;
use async_utils::hanging_get::client::HangingGetStream;
use euclid::size2;
use fidl::endpoints::{create_endpoints, create_proxy, create_request_stream};
use fidl_fuchsia_ui_composition as flatland;
use fidl_fuchsia_ui_views::ViewRef;
use fuchsia_async::{self as fasync, OnSignals};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_framebuffer::{sysmem::BufferCollectionAllocator, FrameSet, FrameUsage, ImageId};
use fuchsia_scenic::BufferCollectionTokenPair;
use fuchsia_trace::{self, duration, instant};
use fuchsia_zircon::{self as zx, Event, HandleBased, Signals, Time};
use futures::{channel::mpsc::UnboundedSender, prelude::*, StreamExt, TryStreamExt};
use std::collections::{BTreeMap, BTreeSet, HashMap};

fn setup_handle_flatland_events(
    event_stream: flatland::FlatlandEventStream,
    view_key: ViewKey,
    app_sender: UnboundedSender<MessageInternal>,
) {
    fasync::Task::local(
        event_stream
            .try_for_each(move |event| {
                match event {
                    flatland::FlatlandEvent::OnNextFrameBegin { values } => {
                        app_sender
                            .unbounded_send(MessageInternal::FlatlandOnNextFrameBegin(
                                view_key, values,
                            ))
                            .expect("unbounded_send");
                    }
                    flatland::FlatlandEvent::OnFramePresented { frame_presented_info } => {
                        app_sender
                            .unbounded_send(MessageInternal::FlatlandOnFramePresented(
                                view_key,
                                frame_presented_info,
                            ))
                            .expect("unbounded_send");
                    }
                    flatland::FlatlandEvent::OnError { error } => {
                        app_sender
                            .unbounded_send(MessageInternal::FlatlandOnError(view_key, error))
                            .expect("unbounded_send");
                    }
                };
                future::ok(())
            })
            .unwrap_or_else(|e| eprintln!("error listening for Flatland Events: {:?}", e)),
    )
    .detach();
}

fn duplicate_import_token(
    token: &fidl_fuchsia_ui_composition::BufferCollectionImportToken,
) -> Result<fidl_fuchsia_ui_composition::BufferCollectionImportToken, Error> {
    let value = token.value.duplicate_handle(zx::Rights::SAME_RIGHTS)?;
    Ok(fidl_fuchsia_ui_composition::BufferCollectionImportToken { value })
}

struct Plumber {
    pub size: UintSize,
    pub collection_id: u32,
    pub frame_set: FrameSet,
    pub image_indexes: BTreeMap<ImageId, u32>,
    pub context: render::Context,
}

impl Plumber {
    async fn new(
        flatland: &flatland::FlatlandProxy,
        allocator: &flatland::AllocatorProxy,
        size: UintSize,
        pixel_format: fidl_fuchsia_sysmem::PixelFormatType,
        buffer_count: usize,
        collection_id: u32,
        first_image_id: u64,
    ) -> Result<Plumber, Error> {
        let use_spinel = Config::get().use_spinel;

        ensure!(use_spinel == false, "Spinel support is disabled");

        let usage = if use_spinel { FrameUsage::Gpu } else { FrameUsage::Cpu };
        let mut buffer_allocator = BufferCollectionAllocator::new(
            size.width,
            size.height,
            pixel_format,
            usage,
            buffer_count,
        )?;

        buffer_allocator.set_name(100, "CarnelianSurface")?;

        let context_token = buffer_allocator.duplicate_token().await?;
        let mut context = render::Context {
            inner: ContextInner::Forma(generic::Forma::new_context(
                context_token,
                size,
                DisplayRotation::Deg0,
            )),
        };

        let sysmem_buffer_collection_token = buffer_allocator.duplicate_token().await?;
        let buffer_tokens = BufferCollectionTokenPair::new();
        let args = flatland::RegisterBufferCollectionArgs {
            export_token: Some(buffer_tokens.export_token),
            buffer_collection_token: Some(sysmem_buffer_collection_token),
            ..flatland::RegisterBufferCollectionArgs::EMPTY
        };

        allocator
            .register_buffer_collection(args)
            .await
            .expect("fidl error")
            .expect("error registering buffer collection");

        let buffers = buffer_allocator.allocate_buffers(true).await.context("allocate_buffers")?;

        let blend_mode = if Config::get().needs_blending {
            flatland::BlendMode::SrcOver
        } else {
            flatland::BlendMode::Src
        };
        let mut image_ids = BTreeSet::new();
        let mut image_indexes = BTreeMap::new();
        for index in 0..buffers.buffer_count as usize {
            let image_id = index + first_image_id as usize;
            image_ids.insert(image_id as u64);
            let uindex = index as u32;
            image_indexes.insert(image_id as u64, uindex);
            let image_props = flatland::ImageProperties {
                size: Some(fidl_fuchsia_math::SizeU { width: size.width, height: size.height }),
                ..flatland::ImageProperties::EMPTY
            };
            let mut flatland_image_id = flatland::ContentId { value: image_id as u64 };
            let mut import_token = duplicate_import_token(&buffer_tokens.import_token)?;
            flatland.create_image(
                &mut flatland_image_id.clone(),
                &mut import_token,
                uindex,
                image_props,
            )?;
            flatland
                .set_image_destination_size(
                    &mut flatland_image_id,
                    &mut fidl_fuchsia_math::SizeU { width: size.width, height: size.height },
                )
                .expect("fidl error");
            flatland
                .set_image_blending_function(&mut flatland_image_id, blend_mode)
                .expect("fidl error");
            // Get all the images at this point, since if we wait until we need them for
            // rendering, Forma's private connection to sysmem has closed and it fails.
            // https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=83543
            context.get_image(index as u32);
        }

        let frame_set = FrameSet::new(collection_id as u64, image_ids);
        Ok(Plumber { size, collection_id, frame_set, image_indexes, context })
    }

    pub fn enter_retirement(&mut self, _flatland: &flatland::FlatlandProxy) {}
}

const RENDER_BUFFER_COUNT: usize = 3;
const DEFAULT_PRESENT_INTERVAL: i64 = (1_000_000_000.0 / 60.0) as i64;
const TRANSFORM_ID: flatland::TransformId = flatland::TransformId { value: 1 };

#[derive(Clone, Copy)]
struct PresentationTime {
    presentation_time: i64,
    latch_point: i64,
}

pub(crate) struct FlatlandViewStrategy {
    flatland: flatland::FlatlandProxy,
    allocator: flatland::AllocatorProxy,
    view_key: ViewKey,
    last_presentation_time: i64,
    future_presentation_times: Vec<PresentationTime>,
    custom_render_offset: Option<i64>,
    present_interval: i64,
    // The number of `Flatland.Present()` calls we're authorized to make.  Initial value is 1.
    // Decremented whenever `Present()` is called, and incremented whenever Scenic authorizes us to.
    num_presents_allowed: usize,
    // Only used for tracing, does not affect behavior.
    pending_present_count: usize,
    render_timer_scheduled: bool,
    missed_frame: bool,
    app_sender: UnboundedSender<MessageInternal>,
    next_buffer_collection: u32,
    plumber: Option<Plumber>,
    retiring_plumbers: Vec<Plumber>,
    next_image_id: u64,
    previous_present_release_event: Option<Event>,
    current_present_release_event: Option<Event>,
    flatland_mouse_input_handlers: HashMap<u32, FlatlandMouseInputHandler>,
    flatland_touch_input_handler: FlatlandTouchInputHandler,
    keyboard_handler: KeyboardInputHandler,
}

impl FlatlandViewStrategy {
    pub(crate) async fn new(
        key: ViewKey,
        flatland_params: FlatlandParams,
        app_sender: UnboundedSender<MessageInternal>,
    ) -> Result<ViewStrategyPtr, Error> {
        let flatland = connect_to_protocol::<flatland::FlatlandMarker>()?;
        if let Some(debug_name) = flatland_params.debug_name {
            flatland.set_debug_name(&debug_name)?;
        }

        // Add trace event with the Flatland channel's koid, so that it can be correlated with a
        // Flatland session within Scenic.
        {
            use fidl::endpoints::Proxy;
            use fuchsia_zircon::AsHandleRef;

            let koid = flatland.as_channel().get_koid().unwrap().raw_koid();
            instant!(
                "gfx",
                "FlatlandViewStrategy::new",
                fuchsia_trace::Scope::Process,
                "flatland_koid" => koid
            );
        }

        flatland.create_transform(&mut TRANSFORM_ID.clone())?;
        flatland.set_root_transform(&mut TRANSFORM_ID.clone())?;
        setup_handle_flatland_events(flatland.take_event_stream(), key, app_sender.clone());
        let allocator = connect_to_protocol::<flatland::AllocatorMarker>()?;
        Self::create_parent_viewport_watcher(
            &flatland,
            app_sender.clone(),
            key,
            flatland_params.args.view_creation_token.expect("view_creation_token"),
        )?;

        let strat = Self {
            flatland,
            allocator,
            view_key: key,
            app_sender: app_sender.clone(),
            last_presentation_time: fasync::Time::now().into_nanos(),
            future_presentation_times: Vec::new(),
            custom_render_offset: None,
            present_interval: DEFAULT_PRESENT_INTERVAL,
            num_presents_allowed: 1,
            pending_present_count: 0,
            render_timer_scheduled: false,
            missed_frame: false,
            plumber: None,
            retiring_plumbers: Vec::new(),
            next_buffer_collection: 1,
            next_image_id: 1,
            previous_present_release_event: None,
            current_present_release_event: None,
            flatland_mouse_input_handlers: HashMap::new(),
            flatland_touch_input_handler: FlatlandTouchInputHandler::default(),
            keyboard_handler: KeyboardInputHandler::new(),
        };

        Ok(Box::new(strat))
    }

    fn create_parent_viewport_watcher(
        flatland: &flatland::FlatlandProxy,
        app_sender: UnboundedSender<MessageInternal>,
        view_key: ViewKey,
        mut view_creation_token: fidl_fuchsia_ui_views::ViewCreationToken,
    ) -> Result<(), Error> {
        let (parent_viewport_watcher, server_end) =
            create_proxy::<flatland::ParentViewportWatcherMarker>()?;

        let viewref_pair = fuchsia_scenic::ViewRefPair::new()?;
        let view_ref = fuchsia_scenic::duplicate_view_ref(&viewref_pair.view_ref)?;

        // Use CreateView2 if input events are expected.
        let input = Config::get().input;
        if input {
            let mut view_identity =
                fidl_fuchsia_ui_views::ViewIdentityOnCreation::from(viewref_pair);
            let mut view_bound_protocols = flatland::ViewBoundProtocols::EMPTY;

            let (touch_client, touch_server) = create_endpoints()?;
            let (mouse_client, mouse_server) = create_endpoints()?;
            let (view_ref_focused_client, view_ref_focused_server) = create_endpoints()?;

            view_bound_protocols.touch_source = Some(touch_server);
            view_bound_protocols.mouse_source = Some(mouse_server);
            view_bound_protocols.view_ref_focused = Some(view_ref_focused_server);

            flatland.create_view2(
                &mut view_creation_token,
                &mut view_identity,
                view_bound_protocols,
                server_end,
            )?;

            let touch_proxy = touch_client.into_proxy()?;
            let touch_sender = app_sender.clone();

            fasync::Task::local(async move {
                let mut events: Vec<fidl_fuchsia_ui_pointer::TouchResponse> = Vec::new();
                loop {
                    let result = touch_proxy.watch(&mut events.into_iter()).await;
                    match result {
                        Ok(returned_events) => {
                            events = returned_events
                                .iter()
                                .map(|event| fidl_fuchsia_ui_pointer::TouchResponse {
                                    trace_flow_id: event
                                        .pointer_sample
                                        .as_ref()
                                        .and_then(|_| event.trace_flow_id),
                                    response_type: event.pointer_sample.as_ref().and_then(|_| {
                                        Some(fidl_fuchsia_ui_pointer::TouchResponseType::Yes)
                                    }),
                                    ..fidl_fuchsia_ui_pointer::TouchResponse::EMPTY
                                })
                                .collect();
                            touch_sender
                                .unbounded_send(MessageInternal::UserInputMessage(
                                    view_key,
                                    UserInputMessage::FlatlandTouchEvents(returned_events),
                                ))
                                .expect("failed to send MessageInternal.");
                        }
                        Err(fidl::Error::ClientChannelClosed { .. }) => {
                            println!("touch event connection closed.");
                            return;
                        }
                        Err(fidl_error) => {
                            println!("touch event connection closed error: {:?}", fidl_error);
                            return;
                        }
                    }
                }
            })
            .detach();

            let mouse_proxy = mouse_client.into_proxy()?;
            let mouse_sender = app_sender.clone();

            fasync::Task::local(async move {
                loop {
                    let result = mouse_proxy.watch().await;
                    match result {
                        Ok(returned_events) => {
                            mouse_sender
                                .unbounded_send(MessageInternal::UserInputMessage(
                                    view_key,
                                    UserInputMessage::FlatlandMouseEvents(returned_events),
                                ))
                                .expect("failed to send MessageInternal.");
                        }
                        Err(fidl::Error::ClientChannelClosed { .. }) => {
                            println!("mouse event connection closed.");
                            return;
                        }
                        Err(fidl_error) => {
                            println!("mouse event connection closed error: {:?}", fidl_error);
                            return;
                        }
                    }
                }
            })
            .detach();

            let view_ref_focused_proxy = view_ref_focused_client.into_proxy()?;
            let view_ref_focused_sender = app_sender.clone();

            fasync::Task::local(async move {
                loop {
                    let result = view_ref_focused_proxy.watch().await;
                    match result {
                        Ok(msg) => {
                            view_ref_focused_sender
                                .unbounded_send(MessageInternal::Focus(
                                    view_key,
                                    msg.focused.unwrap_or(false),
                                ))
                                .expect("failed to send MessageInternal.");
                        }
                        Err(fidl::Error::ClientChannelClosed { .. }) => {
                            println!("ViewRefFocused connection closed.");
                            return;
                        }
                        Err(fidl_error) => {
                            println!("ViewRefFocused connection closed error: {:?}", fidl_error);
                            return;
                        }
                    }
                }
            })
            .detach();

            Self::listen_for_key_events(view_ref, &app_sender, view_key)?;
        } else {
            flatland.create_view(&mut view_creation_token, server_end)?;
        }

        let sender = app_sender.clone();
        fasync::Task::local(async move {
            let mut layout_info_stream = HangingGetStream::new(
                parent_viewport_watcher,
                flatland::ParentViewportWatcherProxy::get_layout,
            );

            while let Some(result) = layout_info_stream.next().await {
                match result {
                    Ok(layout_info) => {
                        if let Some(fidl_fuchsia_math::SizeU { width, height }) =
                            layout_info.logical_size
                        {
                            sender
                                .unbounded_send(MessageInternal::SizeChanged(
                                    view_key,
                                    size2(width, height).to_f32(),
                                ))
                                .expect("failed to send MessageInternal.");
                        }
                        if let Some(fidl_fuchsia_math::VecF { x, y }) =
                            layout_info.device_pixel_ratio
                        {
                            sender
                                .unbounded_send(MessageInternal::MetricsChanged(
                                    view_key,
                                    size2(x, y),
                                ))
                                .expect("failed to send MessageInternal.");
                        }
                    }
                    Err(fidl::Error::ClientChannelClosed { .. }) => {
                        println!("graph link connection closed.");
                        return; // from spawned task closure
                    }
                    Err(fidl_error) => {
                        println!("graph link GetLayout() error: {:?}", fidl_error);
                        return; // from spawned task closure
                    }
                }
            }
        })
        .detach();
        Ok(())
    }

    fn do_present(
        flatland: &flatland::FlatlandProxy,
        _app_sender: &UnboundedSender<MessageInternal>,
        _key: ViewKey,
        presentation_time: i64,
        release_event: Option<Event>,
    ) {
        flatland
            .present(flatland::PresentArgs {
                requested_presentation_time: Some(presentation_time),
                acquire_fences: None,
                release_fences: release_event.and_then(|release_event| Some(vec![release_event])),
                unsquashable: Some(true),
                ..flatland::PresentArgs::EMPTY
            })
            .expect("present error");
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
            buffer_count: None,
            image_id,
            image_index,
            app_sender,
            mouse_cursor_position: None,
            display_info: None,
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
                &self.flatland,
                &self.allocator,
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
            // Conservative render offset to prefer throughput over low-latency.
            // TODO: allow applications that prefer low-latency to control this
            // dynamically.
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

    fn render_to_image_from_plumber(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
    ) -> bool {
        let presentation_time = self.next_presentation_time();
        let plumber = self.plumber.as_mut().expect("plumber");
        if let Some(available) = plumber.frame_set.get_available_image() {
            duration!("gfx", "FlatlandViewStrategy::render.render_to_image");
            let available_index = plumber.image_indexes.get(&available).expect("index for image");
            let render_context = Self::make_view_assistant_context_with_time(
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
            let release_event = Event::create().expect("Event.create");
            let local_release_event =
                release_event.duplicate_handle(zx::Rights::SAME_RIGHTS).expect("duplicate_handle");
            let app_sender = self.app_sender.clone();
            fasync::Task::local(async move {
                let signals = OnSignals::new(&local_release_event, Signals::EVENT_SIGNALED);
                signals.await.expect("to wait");
                app_sender
                    .unbounded_send(MessageInternal::ImageFreed(key, available, collection_id))
                    .expect("unbounded_send");
            })
            .detach();
            self.current_present_release_event = Some(release_event);

            let mut image_id = flatland::ContentId { value: available };
            self.flatland
                .set_content(&mut TRANSFORM_ID.clone(), &mut image_id)
                .expect("fidl error");

            // Image is guaranteed to be presented at this point.
            plumber.frame_set.mark_presented(available);
            true
        } else {
            instant!(
                "gfx",
                "FlatlandViewStrategy::no_available_image",
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
                            .unbounded_send(MessageInternal::UserInputMessage(
                                key,
                                UserInputMessage::ScenicKeyEvent(event),
                            ))
                            .expect("unbounded_send");
                    }
                }
            }
        })
        .detach();
        Ok(())
    }
}

#[async_trait(?Send)]
impl ViewStrategy for FlatlandViewStrategy {
    fn initial_metrics(&self) -> Size {
        size2(1.0, 1.0)
    }

    fn create_view_assistant_context(&self, view_details: &ViewDetails) -> ViewAssistantContext {
        ViewAssistantContext {
            key: view_details.key,
            size: view_details.logical_size,
            metrics: view_details.metrics,
            presentation_time: Default::default(),
            buffer_count: None,
            image_id: Default::default(),
            image_index: Default::default(),
            app_sender: self.app_sender.clone(),
            mouse_cursor_position: None,
            display_info: None,
        }
    }

    fn setup(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        duration!("gfx", "FlatlandViewStrategy::setup");
        let render_context =
            Self::make_view_assistant_context(view_details, 0, 0, self.app_sender.clone());
        view_assistant.setup(&render_context).unwrap_or_else(|e| panic!("Setup error: {:?}", e));
        self.custom_render_offset = view_assistant.get_render_offset();
        self.render_requested();
    }

    async fn render(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
    ) -> bool {
        let size = view_details.physical_size.floor().to_u32();
        duration!("gfx", "FlatlandViewStrategy::render",
            "width" => size.width,
            "height" => size.height);

        self.render_timer_scheduled = false;
        if size.width > 0 && size.height > 0 {
            if self.num_presents_allowed == 0 {
                instant!("gfx", "FlatlandViewStrategy::present_is_not_allowed",
                    fuchsia_trace::Scope::Process,
                    "counts" => format!("{} pending {} allowed",
                        self.pending_present_count,
                        self.num_presents_allowed).as_str());
                self.missed_frame = true;
                return false;
            }

            if self.plumber.is_none() {
                duration!("gfx", "FlatlandViewStrategy::render.create_plumber");
                self.create_plumber(size).await.expect("create_plumber");
            } else {
                let current_size = self.plumber.as_ref().expect("plumber").size;
                if current_size != size {
                    duration!("gfx", "FlatlandViewStrategy::render.create_plumber");
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
        duration!("gfx", "FlatlandViewStrategy::present");
        if !self.missed_frame {
            assert!(self.num_presents_allowed > 0);
            let release_event = self.previous_present_release_event.take();
            let presentation_time = self.next_presentation_time();
            Self::do_present(
                &self.flatland,
                &self.app_sender,
                self.view_key,
                presentation_time.presentation_time,
                release_event,
            );
            self.last_presentation_time = presentation_time.presentation_time;
            self.pending_present_count += 1;
            self.num_presents_allowed -= 1;
            self.previous_present_release_event = self.current_present_release_event.take();
        }
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
        instant!(
            "gfx",
            "FlatlandViewStrategy::present_done",
            fuchsia_trace::Scope::Process,
            "presents" => format!("{} handled", num_presents_handled).as_str()
        );
    }

    fn handle_focus(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        focus: bool,
    ) {
        let mut render_context =
            Self::make_view_assistant_context(view_details, 0, 0, self.app_sender.clone());
        view_assistant
            .handle_focus_event(&mut render_context, focus)
            .unwrap_or_else(|e| panic!("handle_focus error: {:?}", e));
    }

    fn convert_user_input_message(
        &mut self,
        _view_details: &ViewDetails,
        message: UserInputMessage,
    ) -> Result<Vec<crate::input::Event>, Error> {
        match message {
            UserInputMessage::ScenicKeyEvent(key_event) => {
                let converted_events = self.keyboard_handler.handle_key_event(&key_event);
                Ok(converted_events)
            }
            UserInputMessage::FlatlandMouseEvents(mouse_events) => {
                let mut events = Vec::new();
                for mouse_event in &mouse_events {
                    if let Some(pointer_sample) = mouse_event.pointer_sample.as_ref() {
                        let device_id = pointer_sample.device_id.expect("device_id");
                        let handler_entry = self
                            .flatland_mouse_input_handlers
                            .entry(device_id)
                            .or_insert_with(|| FlatlandMouseInputHandler::new(device_id));
                        events.extend(handler_entry.handle_mouse_events(&mouse_events));
                    }
                }
                Ok(events)
            }
            UserInputMessage::FlatlandTouchEvents(touch_events) => {
                Ok(self.flatland_touch_input_handler.handle_events(&touch_events))
            }
            _ => bail!(
                "FlatlandViewStrategy::convert_user_input_message does not support {:?}.",
                message
            ),
        }
    }

    fn image_freed(&mut self, image_id: u64, collection_id: u32) {
        instant!(
            "gfx",
            "FlatlandViewStrategy::image_freed",
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
                    retired_plumber.enter_retirement(&self.flatland);
                }
            }
        }

        self.retiring_plumbers.retain(|plumber| !plumber.frame_set.no_images_in_use());
    }

    fn render_requested(&mut self) {
        self.schedule_render_timer();
    }

    fn handle_on_next_frame_begin(
        &mut self,
        info: &fidl_fuchsia_ui_composition::OnNextFrameBeginValues,
    ) {
        self.num_presents_allowed += info.additional_present_credits.unwrap_or(0) as usize;
        let future_presentation_infos =
            info.future_presentation_infos.as_ref().expect("future_presentation_infos");
        let present_intervals = future_presentation_infos.len();
        instant!(
            "gfx",
            "FlatlandViewStrategy::handle_on_next_frame_begin",
            fuchsia_trace::Scope::Process,
            "counts" => format!("{} present_intervals", present_intervals).as_str()
        );
        if present_intervals > 0 {
            let times: Vec<_> = future_presentation_infos
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
            future_presentation_infos.iter().map(|t| PresentationTime {
                presentation_time: t.presentation_time.expect("presentation_time"),
                latch_point: t.latch_point.expect("latch_point"),
            }),
        );

        // Now that we have incremented `self.num_presents_allowed`, a missed frame might succeed.
        self.retry_missed_frame();
    }
}
