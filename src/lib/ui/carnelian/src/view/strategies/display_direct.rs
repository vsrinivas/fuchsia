// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{
        strategies::framebuffer::{ControllerProxyPtr, DisplayId},
        Config, MessageInternal,
    },
    drawing::DisplayRotation,
    input,
    render::{
        generic::{self, Backend},
        Context as RenderContext, ContextInner,
    },
    view::{
        strategies::base::{ViewStrategy, ViewStrategyPtr},
        DisplayInfo, ViewAssistantContext, ViewAssistantPtr, ViewDetails,
    },
    IntPoint, IntSize, Message, Size, ViewKey,
};
use anyhow::{ensure, Context, Error};
use async_trait::async_trait;
use euclid::size2;
use fidl_fuchsia_hardware_display::{ControllerEvent, ControllerProxy, ImageConfig};
use fuchsia_async::{self as fasync, OnSignals};
use fuchsia_framebuffer::{
    sysmem::BufferCollectionAllocator, FrameSet, FrameUsage, ImageId, PixelFormat,
};
use fuchsia_trace::{duration, instant};
use fuchsia_zircon::{
    self as zx, AsHandleRef, Duration, Event, HandleBased, Signals, Status, Time,
};
use futures::channel::mpsc::UnboundedSender;
use std::{
    collections::{BTreeMap, BTreeSet},
    sync::atomic::{AtomicU64, Ordering},
};

type WaitEvents = BTreeMap<ImageId, (Event, u64)>;

#[derive(Default)]
struct CollectionIdGenerator {}

impl Iterator for CollectionIdGenerator {
    type Item = u64;

    fn next(&mut self) -> Option<u64> {
        static NEXT_ID: AtomicU64 = AtomicU64::new(100);
        let id = NEXT_ID.fetch_add(1, Ordering::SeqCst);
        // fetch_add wraps on overflow, which we'll use as a signal
        // that this generator is out of ids.
        if id == 0 {
            None
        } else {
            Some(id)
        }
    }
}
fn next_collection_id() -> u64 {
    CollectionIdGenerator::default().next().expect("collection_id")
}

async fn create_and_import_event(controller: &ControllerProxy) -> Result<(Event, u64), Error> {
    let event = Event::create()?;

    let their_event = event.duplicate_handle(zx::Rights::SAME_RIGHTS)?;
    let event_id = event.get_koid()?.raw_koid();
    controller.import_event(Event::from_handle(their_event.into_handle()), event_id)?;
    Ok((event, event_id))
}

fn size_from_info(info: &fidl_fuchsia_hardware_display::Info) -> IntSize {
    let mode = info.modes[0];
    size2(mode.horizontal_resolution, mode.vertical_resolution).to_i32()
}

#[derive(Debug, Clone)]
pub struct Display {
    pub controller: ControllerProxyPtr,
    pub display_id: DisplayId,
    pub info: fidl_fuchsia_hardware_display::Info,
    pub layer_id: u64,
}

impl Display {
    pub async fn new(
        controller: ControllerProxyPtr,
        display_id: DisplayId,
        info: fidl_fuchsia_hardware_display::Info,
    ) -> Result<Self, Error> {
        Ok(Self { controller, display_id, info, layer_id: 0 })
    }

    pub async fn create_layer(&mut self) -> Result<(), Error> {
        let (status, layer_id) = self.controller.create_layer().await?;
        ensure!(
            status == zx::sys::ZX_OK,
            "Display::new(): failed to create layer {}",
            Status::from_raw(status)
        );
        self.layer_id = layer_id;
        Ok(())
    }

    pub fn size(&self) -> IntSize {
        size_from_info(&self.info)
    }

    pub fn pixel_format(&self) -> PixelFormat {
        self.info.pixel_format[0].into()
    }
}

struct DisplayResources {
    pub frame_set: FrameSet,
    pub image_indexes: BTreeMap<ImageId, u32>,
    pub context: RenderContext,
    pub wait_events: WaitEvents,
    pub signal_events: WaitEvents,
}

const RENDER_FRAME_COUNT: usize = 2;

pub(crate) struct DisplayDirectViewStrategy {
    display: Display,
    app_sender: UnboundedSender<MessageInternal>,
    display_rotation: DisplayRotation,
    display_resources: Option<DisplayResources>,
    drop_display_resources_task: Option<fasync::Task<()>>,
    display_resource_release_delay: std::time::Duration,
    vsync_phase: Time,
    vsync_interval: Duration,
    mouse_cursor_position: Option<IntPoint>,
    pub collection_id: u64,
    render_frame_count: usize,
    presented: Option<u64>,
}

impl DisplayDirectViewStrategy {
    pub async fn new(
        controller: ControllerProxyPtr,
        app_sender: UnboundedSender<MessageInternal>,
        info: fidl_fuchsia_hardware_display::Info,
    ) -> Result<ViewStrategyPtr, Error> {
        let app_config = Config::get();
        let collection_id = next_collection_id();
        let render_frame_count = app_config.buffer_count.unwrap_or(RENDER_FRAME_COUNT);

        let mut display = Display::new(controller, info.id, info).await?;

        display.create_layer().await?;

        let display_resources = Self::allocate_display_resources(
            collection_id,
            display.size(),
            display.pixel_format(),
            render_frame_count,
            &display,
        )
        .await?;

        let key = display.display_id as ViewKey;
        app_sender.unbounded_send(MessageInternal::Render(key)).expect("unbounded_send");
        app_sender.unbounded_send(MessageInternal::Focus(key)).expect("unbounded_send");

        Ok(Box::new(Self {
            display,
            app_sender,
            display_rotation: app_config.display_rotation,
            display_resources: Some(display_resources),
            drop_display_resources_task: None,
            display_resource_release_delay: app_config.display_resource_release_delay,
            vsync_phase: Time::get_monotonic(),
            vsync_interval: Duration::from_millis(16),
            mouse_cursor_position: None,
            collection_id,
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

        let display_rotation = self.display_rotation;
        let app_sender = self.app_sender.clone();
        let mouse_cursor_position = self.mouse_cursor_position.clone();
        let (image_index, actual_image_id) = image_id
            .and_then(|available| {
                Some((
                    *self.display_resources().image_indexes.get(&available).expect("image_index"),
                    available,
                ))
            })
            .unwrap_or_default();

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
            buffer_count: None,
            image_id: actual_image_id,
            image_index: image_index,
            app_sender,
            mouse_cursor_position,
            display_info: Some(DisplayInfo::from(&self.display.info)),
        }
    }

    async fn allocate_display_resources(
        collection_id: u64,
        size: IntSize,
        pixel_format: fuchsia_framebuffer::PixelFormat,
        render_frame_count: usize,
        display: &Display,
    ) -> Result<DisplayResources, Error> {
        let app_config = Config::get();
        let use_spinel = app_config.use_spinel;
        let display_rotation = app_config.display_rotation;
        let unsize = size.floor().to_u32();

        let usage = if use_spinel { FrameUsage::Gpu } else { FrameUsage::Cpu };
        let mut buffer_allocator = BufferCollectionAllocator::new(
            unsize.width,
            unsize.height,
            pixel_format.into(),
            usage,
            render_frame_count,
        )?;

        buffer_allocator.set_name(100, "CarnelianDirect")?;

        let context_token = buffer_allocator.duplicate_token().await?;
        let context = RenderContext {
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
        };

        let direct_pixel_format = if use_spinel { context.pixel_format() } else { pixel_format };

        let mut image_config = ImageConfig {
            width: unsize.width,
            height: unsize.height,
            pixel_format: direct_pixel_format.into(),
            type_: 0,
        };

        let controller_token = buffer_allocator.duplicate_token().await?;
        display.controller.import_buffer_collection(collection_id, controller_token).await?;
        display
            .controller
            .set_buffer_collection_constraints(collection_id, &mut image_config)
            .await?;

        let buffers = buffer_allocator
            .allocate_buffers(true)
            .await
            .context(format!("view: {} allocate_buffers", display.display_id))?;

        ensure!(buffers.settings.has_image_format_constraints, "No image format constraints");
        ensure!(
            buffers.buffer_count as usize == render_frame_count,
            "Buffers do not match frame count"
        );

        let image_type =
            if buffers.settings.image_format_constraints.pixel_format.has_format_modifier {
                match buffers.settings.image_format_constraints.pixel_format.format_modifier.value {
                    fidl_fuchsia_sysmem::FORMAT_MODIFIER_INTEL_I915_X_TILED => 1,
                    fidl_fuchsia_sysmem::FORMAT_MODIFIER_INTEL_I915_Y_TILED => 2,
                    _ => 0,
                }
            } else {
                0
            };

        image_config.type_ = image_type;

        let mut image_ids = BTreeSet::new();
        let mut image_indexes = BTreeMap::new();
        let mut wait_events = WaitEvents::new();
        let mut signal_events = WaitEvents::new();
        for index in 0..buffers.buffer_count as usize {
            let uindex = index as u32;
            let (status, image_id) = display
                .controller
                .import_image(&mut image_config, collection_id, uindex)
                .await
                .context("controller import_image")?;
            ensure!(status == 0, "import_image error {} ({})", Status::from_raw(status), status);

            image_ids.insert(image_id as u64);
            image_indexes.insert(image_id as u64, uindex);

            let (event, event_id) = create_and_import_event(&display.controller).await?;
            wait_events.insert(image_id as ImageId, (event, event_id));
            let (event, event_id) = create_and_import_event(&display.controller).await?;
            signal_events.insert(image_id as ImageId, (event, event_id));
        }

        let frame_set = FrameSet::new(collection_id as u64, image_ids);

        display.controller.set_layer_primary_config(display.layer_id, &mut image_config)?;

        Ok(DisplayResources { context, image_indexes, frame_set, wait_events, signal_events })
    }

    async fn maybe_reallocate_display_resources(&mut self) -> Result<(), Error> {
        if self.display_resources.is_none() {
            instant!(
                "gfx",
                "DisplayDirectViewStrategy::allocate_display_resources",
                fuchsia_trace::Scope::Process,
                "" => ""
            );
            self.collection_id = next_collection_id();
            self.presented = None;
            self.display_resources = Some(
                Self::allocate_display_resources(
                    self.collection_id,
                    self.display.size(),
                    self.display.pixel_format(),
                    self.render_frame_count,
                    &self.display,
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
            "DisplayDirectViewStrategy::update_image",
            fuchsia_trace::Scope::Process,
            "image" => format!("{}", image).as_str()
        );
        let (event, _) = self.display_resources().wait_events.get(&image).expect("wait event");
        let buffer_ready_event =
            event.duplicate_handle(zx::Rights::SAME_RIGHTS).expect("duplicate_handle");
        let direct_context = self.make_context(view_details, Some(image));

        view_assistant
            .render(&mut self.display_resources().context, buffer_ready_event, &direct_context)
            .unwrap_or_else(|e| panic!("Update error: {:?}", e));
    }

    fn duplicate_signal_event(&mut self, image_id: u64) -> Result<Event, Error> {
        let (signal_event, _) =
            self.display_resources().signal_events.get(&image_id).expect("signal event");
        let local_event =
            signal_event.duplicate_handle(zx::Rights::SAME_RIGHTS).expect("duplicate_handle");
        local_event.as_handle_ref().signal(Signals::EVENT_SIGNALED, Signals::NONE).expect("signal");
        Ok(local_event)
    }

    fn handle_vsync_parameters_changed(&mut self, phase: Time, interval: Duration) {
        self.vsync_phase = phase;
        self.vsync_interval = interval;
    }
}

#[async_trait(?Send)]
impl ViewStrategy for DisplayDirectViewStrategy {
    fn initial_metrics(&self) -> Size {
        size2(1.0, 1.0)
    }

    fn initial_physical_size(&self) -> Size {
        self.display.size().to_f32()
    }

    fn initial_logical_size(&self) -> Size {
        self.display.size().to_f32()
    }

    fn setup(&mut self, view_details: &ViewDetails, view_assistant: &mut ViewAssistantPtr) {
        if let Some(available) = self.display_resources().frame_set.get_available_image() {
            let direct_context = self.make_context(view_details, Some(available));
            view_assistant
                .setup(&direct_context)
                .unwrap_or_else(|e| panic!("Setup error: {:?}", e));
            self.display_resources().frame_set.return_image(available);
        }
    }

    async fn render(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
    ) -> bool {
        duration!("gfx", "DisplayDirectViewStrategy::update");
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

    fn present(&mut self, view_details: &ViewDetails) {
        duration!("gfx", "DisplayDirectViewStrategy::present");
        if self.render_frame_count == 1 && self.presented.is_some() {
            return;
        }
        if let Some(prepared) = self.display_resources().frame_set.prepared {
            instant!(
                "gfx",
                "DisplayDirectViewStrategy::present",
                fuchsia_trace::Scope::Process,
                "prepared" => format!("{}", prepared).as_str()
            );
            let signal_sender = self.app_sender.clone();
            let collection_id = self.collection_id;
            let view_key = view_details.key;
            self.display
                .controller
                .set_display_layers(self.display.display_id, &[self.display.layer_id])
                .expect("set_display_layers");
            let (_, wait_event_id) =
                *self.display_resources().wait_events.get(&prepared).expect("wait event");
            let (_, signal_event_id) =
                *self.display_resources().signal_events.get(&prepared).expect("signal event");
            let image_id = prepared;
            self.display
                .controller
                .set_layer_image(self.display.layer_id, prepared, wait_event_id, signal_event_id)
                .expect("Frame::present() set_layer_image");
            self.display.controller.apply_config().expect("Frame::present() apply_config");
            let local_event =
                self.duplicate_signal_event(prepared).expect("duplicate_signal_event");
            fasync::Task::local(async move {
                let signals = OnSignals::new(&local_event, Signals::EVENT_SIGNALED);
                signals.await.expect("to wait");
                signal_sender
                    .unbounded_send(MessageInternal::ImageFreed(
                        view_key,
                        image_id,
                        collection_id as u32,
                    ))
                    .expect("unbounded_send");
            })
            .detach();
            self.display_resources().frame_set.mark_presented(prepared);
            self.presented = Some(prepared);
        }
    }

    fn handle_focus(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
        focus: bool,
    ) {
        let mut direct_context = self.make_context(view_details, None);
        view_assistant
            .handle_focus_event(&mut direct_context, focus)
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
        let mut direct_context = self.make_context(view_details, None);
        view_assistant
            .handle_input_event(&mut direct_context, &event)
            .unwrap_or_else(|e| eprintln!("handle_new_input_event: {:?}", e));

        direct_context.messages
    }

    fn image_freed(&mut self, image_id: u64, collection_id: u32) {
        if collection_id as u64 == self.collection_id {
            instant!(
                "gfx",
                "DisplayDirectViewStrategy::image_freed",
                fuchsia_trace::Scope::Process,
                "image_freed" => format!("{}", image_id).as_str()
            );
            if let Some(display_resources) = self.display_resources.as_mut() {
                display_resources.frame_set.mark_done_presenting(image_id);
            }
        }
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
                "DisplayDirectViewStrategy::drop_display_resources",
                fuchsia_trace::Scope::Process,
                "" => ""
            );
            self.display_resources = None;
        }
    }

    async fn handle_display_controller_event(&mut self, event: ControllerEvent) {
        match event {
            ControllerEvent::OnVsync { timestamp, cookie, .. } => {
                duration!("gfx", "DisplayDirectViewStrategy::OnVsync");
                let vsync_interval = Duration::from_nanos(
                    100_000_000_000 / self.display.info.modes[0].refresh_rate_e2 as i64,
                );
                self.handle_vsync_parameters_changed(
                    Time::from_nanos(timestamp as i64),
                    vsync_interval,
                );
                if cookie != 0 {
                    self.display.controller.acknowledge_vsync(cookie).expect("acknowledge_vsync");
                }
                self.app_sender
                    .unbounded_send(MessageInternal::Render(self.display.display_id as ViewKey))
                    .expect("unbounded_send");
            }
            _ => (),
        }
    }

    fn close(&mut self) {
        self.display
            .controller
            .release_buffer_collection(self.collection_id)
            .expect("release_buffer_collection");
    }
}
