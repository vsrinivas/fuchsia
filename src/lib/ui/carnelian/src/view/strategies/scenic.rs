// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::{
    app::{MessageInternal, RenderOptions},
    geometry::UintSize,
    input::ScenicInputHandler,
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
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_images::{ImagePipe2Marker, ImagePipe2Proxy};
use fidl_fuchsia_sysmem::ImageFormat2;
use fidl_fuchsia_ui_gfx::{self as gfx};
use fidl_fuchsia_ui_input::SetHardKeyboardDeliveryCmd;
use fidl_fuchsia_ui_views::{ViewRef, ViewRefControl, ViewToken};
use fuchsia_async::{self as fasync, OnSignals};
use fuchsia_framebuffer::{sysmem::BufferCollectionAllocator, FrameSet, FrameUsage, ImageId};
use fuchsia_scenic::{EntityNode, ImagePipe2, Material, Rectangle, SessionPtr, ShapeNode};
use fuchsia_trace::{self, duration, instant};
use fuchsia_zircon::{self as zx, ClockId, Event, HandleBased, Signals, Time};
use futures::{channel::mpsc::UnboundedSender, TryStreamExt};
use std::{
    collections::{BTreeMap, BTreeSet},
    iter,
};

struct Plumber {
    pub size: UintSize,
    pub buffer_count: usize,
    pub collection_id: u32,
    pub first_image_id: u64,
    pub frame_set: FrameSet,
    pub image_indexes: BTreeMap<ImageId, u32>,
    pub context: render::Context,
}

impl Plumber {
    async fn new(
        size: UintSize,
        pixel_format: fidl_fuchsia_sysmem::PixelFormatType,
        buffer_count: usize,
        collection_id: u32,
        first_image_id: u64,
        image_pipe_client: &mut ImagePipe2Proxy,
        render_options: RenderOptions,
    ) -> Result<Plumber, Error> {
        let usage = if render_options.use_spinel { FrameUsage::Gpu } else { FrameUsage::Cpu };
        let mut buffer_allocator = BufferCollectionAllocator::new(
            size.width,
            size.height,
            pixel_format,
            usage,
            buffer_count,
        )?;
        let image_pipe_token = buffer_allocator.duplicate_token().await?;
        image_pipe_client.add_buffer_collection(collection_id, image_pipe_token)?;
        let context_token = buffer_allocator.duplicate_token().await?;
        let mut context = render::Context {
            inner: if render_options.use_spinel {
                ContextInner::Spinel(generic::Spinel::new_context(context_token, size))
            } else {
                ContextInner::Mold(generic::Mold::new_context(context_token, size))
            },
        };
        let buffers = buffer_allocator.allocate_buffers(true).await.context("allocate_buffers")?;
        let buffers_pixel_format = if render_options.use_spinel {
            match context.pixel_format() {
                fuchsia_framebuffer::PixelFormat::Abgr8888 => {
                    fidl_fuchsia_sysmem::PixelFormatType::R8G8B8A8
                }
                fuchsia_framebuffer::PixelFormat::Argb8888 => {
                    fidl_fuchsia_sysmem::PixelFormatType::Bgra32
                }
                fuchsia_framebuffer::PixelFormat::RgbX888 => {
                    fidl_fuchsia_sysmem::PixelFormatType::Bgra32
                }
                fuchsia_framebuffer::PixelFormat::BgrX888 => {
                    fidl_fuchsia_sysmem::PixelFormatType::R8G8B8A8
                }
                _ => fidl_fuchsia_sysmem::PixelFormatType::Invalid,
            }
        } else {
            pixel_format
        };

        let mut image_ids = BTreeSet::new();
        let mut image_indexes = BTreeMap::new();
        let mut index = 0;
        for _buffer in &buffers.buffers[0..buffers.buffer_count as usize] {
            let image_id = index + first_image_id;
            image_ids.insert(image_id);
            let uindex = index as u32;
            // Realized the image before passing it to image pipe.
            let _render_image = context.get_image(uindex);
            image_pipe_client
                .add_image(
                    image_id as u32,
                    collection_id,
                    uindex,
                    &mut make_image_format(size.width, size.height, buffers_pixel_format),
                )
                .expect("add_image");
            image_indexes.insert(image_id, uindex);
            index += 1;
        }
        let frame_set = FrameSet::new(image_ids);
        Ok(Plumber {
            size,
            buffer_count,
            collection_id,
            first_image_id,
            frame_set,
            image_indexes,
            context,
        })
    }

    pub fn enter_retirement(&mut self, image_pipe_client: &mut ImagePipe2Proxy) {
        for image_id in self.first_image_id..self.first_image_id + self.buffer_count as u64 {
            image_pipe_client.remove_image(image_id as u32).unwrap_or_else(|err| {
                eprintln!("image_pipe_client.remove_image {} failed with {}", image_id, err)
            });
        }
        image_pipe_client.remove_buffer_collection(self.collection_id).unwrap_or_else(|err| {
            eprintln!(
                "image_pipe_client.remove_buffer_collection {} failed with {}",
                self.collection_id, err
            )
        });
    }
}

const RENDER_BUFFER_COUNT: usize = 3;

pub(crate) struct ScenicViewStrategy {
    #[allow(unused)]
    render_options: RenderOptions,
    #[allow(unused)]
    view: View,
    #[allow(unused)]
    root_node: EntityNode,
    session: SessionPtr,
    view_key: ViewKey,
    pending_present_count: usize,
    next_presentation_times: Vec<i64>,
    last_presentation_time: i64,
    remaining_presents_in_flight_allowed: i64,
    render_timer_scheduled: bool,
    missed_frame: bool,
    app_sender: UnboundedSender<MessageInternal>,
    content_node: ShapeNode,
    content_material: Material,
    next_buffer_collection: u32,
    image_pipe: ImagePipe2,
    image_pipe_client: ImagePipe2Proxy,
    plumber: Option<Plumber>,
    retiring_plumbers: Vec<Plumber>,
    next_image_id: u64,
    input_handler: ScenicInputHandler,
}

impl ScenicViewStrategy {
    pub(crate) async fn new(
        key: ViewKey,
        session: &SessionPtr,
        render_options: RenderOptions,
        view_token: ViewToken,
        control_ref: ViewRefControl,
        view_ref: ViewRef,
        app_sender: UnboundedSender<MessageInternal>,
    ) -> Result<ViewStrategyPtr, Error> {
        let (image_pipe_client, server_end) = create_endpoints::<ImagePipe2Marker>()?;
        let image_pipe = ImagePipe2::new(session.clone(), server_end);
        let content_material = Material::new(session.clone());
        let content_node = ShapeNode::new(session.clone());
        content_node.set_material(&content_material);
        let image_pipe_client = image_pipe_client.into_proxy()?;
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

        session.lock().enqueue(fidl_fuchsia_ui_scenic::Command::Input(
            fidl_fuchsia_ui_input::Command::SetHardKeyboardDelivery(SetHardKeyboardDeliveryCmd {
                delivery_request: true,
            }),
        ));

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

        let present_sender = app_sender.clone();
        let info_fut = session.lock().present2(0, 0);
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

        let strat = ScenicViewStrategy {
            view,
            session: session.clone(),
            view_key: key,
            app_sender: app_sender.clone(),
            pending_present_count: 1,
            next_presentation_times: Vec::new(),
            last_presentation_time: 0,
            remaining_presents_in_flight_allowed: 3,
            render_timer_scheduled: false,
            missed_frame: false,
            root_node,
            render_options,
            image_pipe_client,
            image_pipe,
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

    fn make_view_assistant_context(
        view_details: &ViewDetails,
        image_id: ImageId,
        image_index: u32,
        app_sender: UnboundedSender<MessageInternal>,
    ) -> ViewAssistantContext {
        ViewAssistantContext {
            key: view_details.key,
            size: view_details.logical_size,
            metrics: view_details.metrics,
            presentation_time: Time::get(ClockId::Monotonic),
            messages: Vec::new(),
            buffer_count: None,
            image_id,
            image_index,
            frame_buffer: None,
            app_sender,
        }
    }

    async fn create_plumber(&mut self, size: UintSize) -> Result<(), Error> {
        let buffer_collection_id = self.next_buffer_collection;
        self.next_buffer_collection = self.next_buffer_collection.wrapping_add(1);
        let next_image_id = self.next_image_id;
        self.next_image_id = self.next_image_id.wrapping_add(RENDER_BUFFER_COUNT as u64);
        self.plumber = Some(
            Plumber::new(
                size.to_u32(),
                fidl_fuchsia_sysmem::PixelFormatType::Bgra32,
                RENDER_BUFFER_COUNT,
                buffer_collection_id,
                next_image_id,
                &mut self.image_pipe_client,
                self.render_options,
            )
            .await
            .expect("VmoPlumber::new"),
        );
        Ok(())
    }

    fn next_presentation_time(&self) -> i64 {
        // TODO: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=60306
        let now = fasync::Time::now().into_nanos();
        let next = self.next_presentation_times.iter().find(|time| **time >= now).unwrap_or(&now);
        *next
    }

    fn schedule_render_timer(&mut self, presentation_time: i64) {
        if !self.render_timer_scheduled {
            let timer = fasync::Timer::new(fuchsia_async::Time::from_nanos(presentation_time));
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
}

fn make_image_format(
    width: u32,
    height: u32,
    pixel_format: fidl_fuchsia_sysmem::PixelFormatType,
) -> ImageFormat2 {
    ImageFormat2 {
        bytes_per_row: width * 4,
        coded_height: height,
        coded_width: width,
        color_space: fidl_fuchsia_sysmem::ColorSpace {
            type_: fidl_fuchsia_sysmem::ColorSpaceType::Srgb,
        },
        display_height: height,
        display_width: width,
        has_pixel_aspect_ratio: false,
        layers: 1,
        pixel_aspect_ratio_height: 1,
        pixel_aspect_ratio_width: 1,
        pixel_format: fidl_fuchsia_sysmem::PixelFormat {
            type_: pixel_format,
            has_format_modifier: true,
            format_modifier: fidl_fuchsia_sysmem::FormatModifier {
                value: fidl_fuchsia_sysmem::FORMAT_MODIFIER_LINEAR,
            },
        },
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
            let plumber = self.plumber.as_mut().expect("plumber");
            let center_x = view_details.physical_size.width * 0.5;
            let center_y = view_details.physical_size.height * 0.5;
            self.content_node.set_translation(center_x, center_y, -0.1);
            let rectangle = Rectangle::new(
                self.session.clone(),
                view_details.physical_size.width as f32,
                view_details.physical_size.height as f32,
            );
            if let Some(available) = plumber.frame_set.get_available_image() {
                self.missed_frame = false;
                duration!("gfx", "ScenicViewStrategy::render.render_to_image");
                let available_index =
                    plumber.image_indexes.get(&available).expect("index for image");
                self.content_node.set_shape(&rectangle);
                let render_context = ScenicViewStrategy::make_view_assistant_context(
                    view_details,
                    available,
                    *available_index,
                    self.app_sender.clone(),
                );
                let buffer_ready_event = Event::create().expect("Event.create");
                let buffer_ready_event_for_image_pipe = buffer_ready_event
                    .duplicate_handle(zx::Rights::SAME_RIGHTS)
                    .expect("duplicate_handle");
                view_assistant
                    .render(&mut plumber.context, buffer_ready_event, &render_context)
                    .unwrap_or_else(|e| panic!("Update error: {:?}", e));
                plumber.frame_set.mark_prepared(available);
                let image_freed_event = Event::create().expect("Event.create");
                let local_event = image_freed_event
                    .duplicate_handle(zx::Rights::SAME_RIGHTS)
                    .expect("duplicate_handle");
                let app_sender = self.app_sender.clone();
                let key = view_details.key;
                let collection_id = plumber.collection_id;
                fasync::Task::local(async move {
                    let signals = OnSignals::new(&local_event, Signals::EVENT_SIGNALED);
                    signals.await.expect("to wait");
                    app_sender
                        .unbounded_send(MessageInternal::ImageFreed(key, available, collection_id))
                        .expect("unbounded_send");
                })
                .detach();
                self.content_material.set_texture_resource(Some(&self.image_pipe));
                let image_present_event = self.image_pipe_client.present_image(
                    available as u32,
                    0,
                    &mut iter::once(buffer_ready_event_for_image_pipe),
                    &mut iter::once(image_freed_event),
                );
                fasync::Task::local(async move {
                    image_present_event.await.expect("to present_image");
                })
                .detach();
                // Image is guaranteed to be presented at this point.
                plumber.frame_set.mark_presented(available);
                true
            } else {
                self.missed_frame = true;
                false
            }
        } else {
            true
        }
    }

    fn present(&mut self, _view_details: &ViewDetails) {
        duration!("gfx", "ScenicViewStrategy::present");
        if self.remaining_presents_in_flight_allowed == 0 {
            instant!(
                "gfx",
                "ScenicViewStrategy::zero_remaining_presents_in_flight_allowed",
                fuchsia_trace::Scope::Process,
                "remaining_presents_in_flight_allowed" => format!("{:?}", self.remaining_presents_in_flight_allowed).as_str()
            );
        } else if self.pending_present_count >= 3 {
            instant!(
                "gfx",
                "ScenicViewStrategy::too_many_presents",
                fuchsia_trace::Scope::Process,
                "pending_present_count" => format!("{:?}", self.pending_present_count).as_str()
            );
        } else {
            let presentation_time = self.next_presentation_time();
            let present_event = self.session.lock().present2(presentation_time, 0);
            let present_sender = self.app_sender.clone();
            let view_key = self.view_key;
            fasync::Task::local(async move {
                match present_event.await {
                    // TODO: figure out how to recover from this error
                    Err(err) => eprintln!("Present Error: {}", err),
                    Ok(info) => {
                        present_sender
                            .unbounded_send(MessageInternal::ScenicPresentSubmitted(view_key, info))
                            .expect("unbounded_send");
                    }
                }
            })
            .detach();
            // Advance presentation time.
            self.pending_present_count += 1;
            self.last_presentation_time = presentation_time;
        }
    }

    fn present_submitted(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
        info: fidl_fuchsia_scenic_scheduling::FuturePresentationTimes,
    ) {
        self.next_presentation_times = info
            .future_presentations
            .iter()
            .skip(1) // Skip the first one, as it is the time we are presenting to.
            .filter_map(|info| info.presentation_time)
            .collect();
        self.remaining_presents_in_flight_allowed = info.remaining_presents_in_flight_allowed;
    }

    fn present_done(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
        _info: fidl_fuchsia_scenic_scheduling::FramePresentedInfo,
    ) {
        assert_ne!(self.pending_present_count, 0);
        self.pending_present_count -= 1;
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

    fn image_freed(&mut self, image_id: u64, collection_id: u32) {
        instant!(
            "gfx",
            "ScenicViewStrategy::image_freed",
            fuchsia_trace::Scope::Process,
            "image_freed" => format!("{} in {}", image_id, collection_id).as_str()
        );

        if self.missed_frame {
            self.render_requested();
        }

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
                    retired_plumber.enter_retirement(&mut self.image_pipe_client);
                }
            }
        }

        self.retiring_plumbers.retain(|plumber| !plumber.frame_set.no_images_in_use());
    }

    fn render_requested(&mut self) {
        self.schedule_render_timer(self.next_presentation_time());
    }
}
