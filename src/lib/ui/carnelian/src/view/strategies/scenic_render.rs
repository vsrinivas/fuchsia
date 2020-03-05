// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::{
    app::MessageInternal,
    canvas::{Canvas, MappingPixelSink},
    geometry::UintSize,
    message::Message,
    render::{
        self,
        generic::{self, Backend},
        ContextInner,
    },
    view::{
        scenic_present, scenic_present_done,
        strategies::base::{ViewStrategy, ViewStrategyPtr},
        ScenicResources, ViewAssistantContext, ViewAssistantPtr, ViewDetails,
    },
};
use anyhow::{Context, Error, Result};
use async_trait::async_trait;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_images::{ImagePipe2Marker, ImagePipe2Proxy};
use fidl_fuchsia_sysmem::ImageFormat2;
use fidl_fuchsia_ui_views::ViewToken;
use fuchsia_async::{self as fasync, OnSignals};
use fuchsia_framebuffer::{sysmem::BufferCollectionAllocator, FrameSet, FrameUsage, ImageId};
use fuchsia_scenic::{ImagePipe2, Material, Rectangle, SessionPtr, ShapeNode};
use fuchsia_zircon::{self as zx, ClockId, Event, HandleBased, Signals, Time};
use futures::channel::mpsc::UnboundedSender;
use std::{
    cell::RefCell,
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
        use_mold: bool,
    ) -> Result<Plumber, Error> {
        let usage = if use_mold { FrameUsage::Cpu } else { FrameUsage::Gpu };
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
            inner: if use_mold {
                ContextInner::Mold(generic::Mold::new_context(context_token, size))
            } else {
                ContextInner::Spinel(generic::Spinel::new_context(context_token, size))
            },
        };
        let buffers = buffer_allocator.allocate_buffers(true).await.context("allocate_buffers")?;
        let buffers_pixel_format = if use_mold {
            pixel_format
        } else {
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

pub(crate) struct RenderViewStrategy {
    #[allow(unused)]
    use_mold: bool,
    scenic_resources: ScenicResources,
    content_node: ShapeNode,
    content_material: Material,
    next_buffer_collection: u32,
    image_pipe: ImagePipe2,
    image_pipe_client: ImagePipe2Proxy,
    plumber: Option<Plumber>,
    retiring_plumbers: Vec<Plumber>,
    next_image_id: u64,
}

impl RenderViewStrategy {
    pub(crate) async fn new(
        session: &SessionPtr,
        use_mold: bool,
        view_token: ViewToken,
        app_sender: UnboundedSender<MessageInternal>,
    ) -> Result<ViewStrategyPtr, Error> {
        let scenic_resources = ScenicResources::new(session, view_token, app_sender);
        let (image_pipe_client, server_end) = create_endpoints::<ImagePipe2Marker>()?;
        let image_pipe = ImagePipe2::new(session.clone(), server_end);
        let content_material = Material::new(session.clone());
        let content_node = ShapeNode::new(session.clone());
        content_node.set_material(&content_material);
        scenic_resources.root_node.add_child(&content_node);
        let image_pipe_client = image_pipe_client.into_proxy()?;
        session.lock().flush();
        let strat = RenderViewStrategy {
            use_mold,
            scenic_resources,
            image_pipe_client,
            image_pipe,
            content_node,
            content_material,
            plumber: None,
            retiring_plumbers: Vec::new(),
            next_buffer_collection: 1,
            next_image_id: 1,
        };
        Ok(Box::new(strat))
    }

    fn make_view_assistant_context<'a>(
        view_details: &ViewDetails,
        image_id: ImageId,
        image_index: u32,
        canvas: Option<&'a RefCell<Canvas<MappingPixelSink>>>,
    ) -> ViewAssistantContext<'a> {
        ViewAssistantContext {
            key: view_details.key,
            logical_size: view_details.logical_size,
            size: view_details.physical_size,
            metrics: view_details.metrics,
            presentation_time: Time::get(ClockId::Monotonic),
            messages: Vec::new(),
            scenic_resources: None,
            canvas: canvas,
            buffer_count: None,
            wait_event: None,
            image_id,
            image_index,
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
                self.use_mold,
            )
            .await
            .expect("VmoPlumber::new"),
        );
        Ok(())
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
impl ViewStrategy for RenderViewStrategy {
    fn setup(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        let canvas_context =
            RenderViewStrategy::make_view_assistant_context(view_details, 0, 0, None);
        view_assistant.setup(&canvas_context).unwrap_or_else(|e| panic!("Setup error: {:?}", e));
    }

    async fn update(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        let size = view_details.logical_size.floor().to_u32();
        if size.width > 0 && size.height > 0 {
            if self.plumber.is_none() {
                self.create_plumber(size).await.expect("create_plumber");
            } else {
                let current_size = self.plumber.as_ref().expect("plumber").size;
                if current_size != size {
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
                self.scenic_resources.session.clone(),
                view_details.physical_size.width as f32,
                view_details.physical_size.height as f32,
            );
            if let Some(available) = plumber.frame_set.get_available_image() {
                let available_index =
                    plumber.image_indexes.get(&available).expect("index for image");
                self.content_node.set_shape(&rectangle);
                let render_context = RenderViewStrategy::make_view_assistant_context(
                    view_details,
                    available,
                    *available_index,
                    None,
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
                let app_sender = self.scenic_resources.app_sender.clone();
                let key = view_details.key;
                let collection_id = plumber.collection_id;
                fasync::spawn_local(async move {
                    let signals = OnSignals::new(&local_event, Signals::EVENT_SIGNALED);
                    signals.await.expect("to wait");
                    app_sender
                        .unbounded_send(MessageInternal::ImageFreed(key, available, collection_id))
                        .expect("unbounded_send");
                });
                self.content_material.set_texture_resource(Some(&self.image_pipe));
                self.image_pipe_client
                    .present_image(
                        available as u32,
                        0,
                        &mut iter::once(buffer_ready_event_for_image_pipe),
                        &mut iter::once(image_freed_event),
                    )
                    .await
                    .expect("#### present_image");
                plumber.frame_set.mark_presented(available);
            }
        }
    }

    fn present(&mut self, view_details: &ViewDetails) {
        scenic_present(&mut self.scenic_resources, view_details.key);
    }

    fn present_done(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
    ) {
        scenic_present_done(&mut self.scenic_resources);
    }

    fn handle_input_event(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        event: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Vec<Message> {
        let mut canvas_context =
            RenderViewStrategy::make_view_assistant_context(view_details, 0, 0, None);
        view_assistant
            .handle_input_event(&mut canvas_context, &event)
            .unwrap_or_else(|e| eprintln!("handle_event: {:?}", e));

        canvas_context.messages
    }

    fn image_freed(&mut self, image_id: u64, collection_id: u32) {
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
}
