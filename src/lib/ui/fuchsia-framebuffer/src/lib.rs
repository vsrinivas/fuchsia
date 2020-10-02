// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fdio::watch_directory;
use fidl::{
    endpoints,
    endpoints::{create_endpoints, ClientEnd},
};
use fidl_fuchsia_hardware_display::{
    ControllerEvent, ControllerMarker, ControllerProxy, ImageConfig, ProviderSynchronousProxy,
};
use fuchsia_async::{self as fasync, DurationExt, OnSignals, TimeoutExt};
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon::{
    self as zx, sys::ZX_TIME_INFINITE, AsHandleRef, DurationNum, Event, HandleBased, Signals,
    Status, Vmo, VmoOp,
};
use futures::{StreamExt, TryFutureExt, TryStreamExt};
use mapped_vmo::Mapping;
use std::fs::OpenOptions;
use std::{
    collections::{BTreeMap, BTreeSet},
    sync::Arc,
};

#[cfg(test)]
use std::ops::Range;

pub mod sysmem;

const BUFFER_COLLECTION_ID: u64 = 1;

#[allow(non_camel_case_types, non_upper_case_globals)]
const ZX_PIXEL_FORMAT_NONE: u32 = 0;
#[allow(non_camel_case_types, non_upper_case_globals)]
const ZX_PIXEL_FORMAT_RGB_565: u32 = 131073;
#[allow(non_camel_case_types, non_upper_case_globals)]
const ZX_PIXEL_FORMAT_RGB_332: u32 = 65538;
#[allow(non_camel_case_types, non_upper_case_globals)]
const ZX_PIXEL_FORMAT_RGB_2220: u32 = 65539;
#[allow(non_camel_case_types, non_upper_case_globals)]
const ZX_PIXEL_FORMAT_ARGB_8888: u32 = 262148;
#[allow(non_camel_case_types, non_upper_case_globals)]
const ZX_PIXEL_FORMAT_RGB_x888: u32 = 262149;
#[allow(non_camel_case_types, non_upper_case_globals)]
const ZX_PIXEL_FORMAT_MONO_8: u32 = 65543;
#[allow(non_camel_case_types, non_upper_case_globals)]
const ZX_PIXEL_FORMAT_GRAY_8: u32 = 65543;
#[allow(non_camel_case_types, non_upper_case_globals)]
const ZX_PIXEL_FORMAT_MONO_1: u32 = 6;
#[allow(non_camel_case_types, non_upper_case_globals)]
const ZX_PIXEL_FORMAT_ABGR_8888: u32 = 262154;
#[allow(non_camel_case_types, non_upper_case_globals)]
const ZX_PIXEL_FORMAT_BGR_x888: u32 = 262155;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PixelFormat {
    Abgr8888,
    Argb8888,
    BgrX888,
    Gray8,
    Mono1,
    Mono8,
    Rgb2220,
    Rgb332,
    Rgb565,
    RgbX888,
    Unknown,
}

impl Default for PixelFormat {
    fn default() -> PixelFormat {
        PixelFormat::Unknown
    }
}

impl From<u32> for PixelFormat {
    fn from(pixel_format: u32) -> Self {
        #[allow(non_upper_case_globals)]
        match pixel_format {
            ZX_PIXEL_FORMAT_ABGR_8888 => PixelFormat::Abgr8888,
            ZX_PIXEL_FORMAT_ARGB_8888 => PixelFormat::Argb8888,
            ZX_PIXEL_FORMAT_BGR_x888 => PixelFormat::BgrX888,
            ZX_PIXEL_FORMAT_MONO_1 => PixelFormat::Mono1,
            ZX_PIXEL_FORMAT_MONO_8 => PixelFormat::Mono8,
            ZX_PIXEL_FORMAT_RGB_2220 => PixelFormat::Rgb2220,
            ZX_PIXEL_FORMAT_RGB_332 => PixelFormat::Rgb332,
            ZX_PIXEL_FORMAT_RGB_565 => PixelFormat::Rgb565,
            ZX_PIXEL_FORMAT_RGB_x888 => PixelFormat::RgbX888,
            // ZX_PIXEL_FORMAT_GRAY_8 is an alias for ZX_PIXEL_FORMAT_MONO_8
            ZX_PIXEL_FORMAT_NONE => PixelFormat::Unknown,
            _ => PixelFormat::Unknown,
        }
    }
}

impl Into<u32> for PixelFormat {
    fn into(self) -> u32 {
        match self {
            PixelFormat::Abgr8888 => ZX_PIXEL_FORMAT_ABGR_8888,
            PixelFormat::Argb8888 => ZX_PIXEL_FORMAT_ARGB_8888,
            PixelFormat::BgrX888 => ZX_PIXEL_FORMAT_BGR_x888,
            PixelFormat::Mono1 => ZX_PIXEL_FORMAT_MONO_1,
            PixelFormat::Mono8 => ZX_PIXEL_FORMAT_MONO_8,
            PixelFormat::Rgb2220 => ZX_PIXEL_FORMAT_RGB_2220,
            PixelFormat::Rgb332 => ZX_PIXEL_FORMAT_RGB_332,
            PixelFormat::Rgb565 => ZX_PIXEL_FORMAT_RGB_565,
            PixelFormat::RgbX888 => ZX_PIXEL_FORMAT_RGB_x888,
            PixelFormat::Gray8 => ZX_PIXEL_FORMAT_GRAY_8,
            PixelFormat::Unknown => ZX_PIXEL_FORMAT_NONE,
        }
    }
}

impl Into<fidl_fuchsia_sysmem::PixelFormatType> for PixelFormat {
    fn into(self) -> fidl_fuchsia_sysmem::PixelFormatType {
        match self {
            PixelFormat::Abgr8888 => fidl_fuchsia_sysmem::PixelFormatType::R8G8B8A8,
            PixelFormat::Argb8888 => fidl_fuchsia_sysmem::PixelFormatType::Bgra32,
            PixelFormat::RgbX888 => fidl_fuchsia_sysmem::PixelFormatType::Bgra32,
            PixelFormat::BgrX888 => fidl_fuchsia_sysmem::PixelFormatType::R8G8B8A8,
            _ => fidl_fuchsia_sysmem::PixelFormatType::Invalid,
        }
    }
}

fn pixel_format_bytes(pixel_format: u32) -> usize {
    ((pixel_format >> 16) & 7) as usize
}

pub type ImageId = u64;

#[derive(Debug)]
pub struct FrameSet {
    image_count: usize,
    available: BTreeSet<ImageId>,
    pub prepared: Option<ImageId>,
    presented: BTreeSet<ImageId>,
}

impl FrameSet {
    pub fn new(available: BTreeSet<ImageId>) -> FrameSet {
        FrameSet {
            image_count: available.len(),
            available,
            prepared: None,
            presented: BTreeSet::new(),
        }
    }

    #[cfg(test)]
    pub fn new_with_range(r: Range<ImageId>) -> FrameSet {
        let mut available = BTreeSet::new();
        for image_id in r {
            available.insert(image_id);
        }
        Self::new(available)
    }

    pub fn mark_presented(&mut self, image_id: ImageId) {
        assert!(
            !self.presented.contains(&image_id),
            "Attempted to mark as presented image {} which was already in the presented image set",
            image_id
        );
        self.presented.insert(image_id);
        self.prepared = None;
    }

    pub fn mark_done_presenting(&mut self, image_id: ImageId) {
        assert!(
            self.presented.remove(&image_id),
            "Attempted to mark as freed image {} which was not the presented image",
            image_id
        );
        self.available.insert(image_id);
    }

    pub fn mark_prepared(&mut self, image_id: ImageId) {
        assert!(self.prepared.is_none(), "Trying to mark image {} as prepared when image {} is prepared and has not been presented", image_id, self.prepared.unwrap());
        self.prepared.replace(image_id);
        self.available.remove(&image_id);
    }

    pub fn get_available_image(&mut self) -> Option<ImageId> {
        let first = self.available.iter().next().map(|a| *a);
        if let Some(first) = first {
            self.available.remove(&first);
        }
        first
    }

    pub fn return_image(&mut self, image_id: ImageId) {
        self.available.insert(image_id);
    }

    pub fn no_images_in_use(&self) -> bool {
        self.available.len() == self.image_count
    }
}

#[cfg(test)]
mod frameset_tests {
    use crate::{FrameSet, ImageId};
    use std::ops::Range;

    const IMAGE_RANGE: Range<ImageId> = 200..202;

    #[test]
    #[should_panic]
    fn test_double_prepare() {
        let mut fs = FrameSet::new_with_range(IMAGE_RANGE);

        fs.mark_prepared(100);
        fs.mark_prepared(200);
    }

    #[test]
    #[should_panic]
    fn test_not_presented() {
        let mut fs = FrameSet::new_with_range(IMAGE_RANGE);
        fs.mark_done_presenting(100);
    }

    #[test]
    #[should_panic]
    fn test_already_presented() {
        let mut fs = FrameSet::new_with_range(IMAGE_RANGE);
        fs.mark_presented(100);
        fs.mark_presented(100);
    }

    #[test]
    fn test_basic_use() {
        let mut fs = FrameSet::new_with_range(IMAGE_RANGE);
        let avail = fs.get_available_image();
        assert!(avail.is_some());
        let avail = avail.unwrap();
        assert!(!fs.available.contains(&avail));
        assert!(!fs.presented.contains(&avail));
        fs.mark_prepared(avail);
        assert_eq!(fs.prepared.unwrap(), avail);
        fs.mark_presented(avail);
        assert!(fs.prepared.is_none());
        assert!(!fs.available.contains(&avail));
        assert!(fs.presented.contains(&avail));
        fs.mark_done_presenting(avail);
        assert!(fs.available.contains(&avail));
        assert!(!fs.presented.contains(&avail));
    }
}

pub fn to_565(pixel: &[u8; 4]) -> [u8; 2] {
    let red = pixel[0] >> 3;
    let green = pixel[1] >> 2;
    let blue = pixel[2] >> 3;
    let b1 = (red << 3) | ((green & 0b11_1000) >> 3);
    let b2 = ((green & 0b111) << 5) | blue;
    [b2, b1]
}

#[derive(Debug, Clone, Copy, Default)]
pub struct Config {
    pub display_id: u64,
    pub width: u32,
    pub height: u32,
    pub refresh_rate_e2: u32,
    pub linear_stride_bytes: u32,
    pub format: PixelFormat,
    pub pixel_size_bytes: u32,
}

impl Config {
    pub fn linear_stride_bytes(&self) -> usize {
        self.linear_stride_bytes as usize
    }
}

// TODO: this should eventually be removed in favor of client adding
// CPU access requirements if needed
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum FrameUsage {
    Cpu,
    Gpu,
}

pub struct Frame {
    config: Config,
    pub image_id: u64,
    pub image_index: u32,
    pub signal_event: Event,
    signal_event_id: u64,
    pub wait_event: Event,
    wait_event_id: u64,
    image_vmo: Vmo,
    pub mapping: Arc<Mapping>,
}

impl Frame {
    fn create_image_config(image_type: u32, config: &Config) -> ImageConfig {
        ImageConfig {
            width: config.width,
            height: config.height,
            pixel_format: config.format.into(),
            type_: image_type,
        }
    }

    async fn import_buffer(
        framebuffer: &FrameBuffer,
        config: &Config,
        image_type: u32,
        index: u32,
    ) -> Result<u64, Error> {
        let mut image_config = Self::create_image_config(image_type, config);

        let (status, image_id) = framebuffer
            .controller
            .import_image(&mut image_config, BUFFER_COLLECTION_ID, index)
            .await
            .context("controller import_image")?;

        if status != 0 {
            return Err(format_err!(
                "import_image error {} ({})",
                Status::from_raw(status),
                status
            ));
        }

        Ok(image_id)
    }

    async fn create_and_import_event(framebuffer: &FrameBuffer) -> Result<(Event, u64), Error> {
        let event = Event::create()?;

        let their_event = event.duplicate_handle(zx::Rights::SAME_RIGHTS)?;
        let event_id = event.get_koid()?.raw_koid();
        framebuffer
            .controller
            .import_event(Event::from_handle(their_event.into_handle()), event_id)?;
        Ok((event, event_id))
    }

    pub(crate) async fn new(
        framebuffer: &FrameBuffer,
        image_vmo: Vmo,
        config: &Config,
        image_type: u32,
        index: u32,
    ) -> Result<Frame, Error> {
        // TODO: don't require a CPU mapping when it is not needed.
        let mapping = match framebuffer.usage {
            FrameUsage::Cpu => Mapping::create_from_vmo(
                &image_vmo,
                framebuffer.byte_size(),
                zx::VmarFlags::PERM_READ
                    | zx::VmarFlags::PERM_WRITE
                    | zx::VmarFlags::MAP_RANGE
                    | zx::VmarFlags::REQUIRE_NON_RESIZABLE,
            )
            .context("Frame::new() Mapping::create_from_vmo failed")?,
            FrameUsage::Gpu => Mapping::allocate(4096).expect("Unused VMO allocation failed").0,
        };

        // import image
        let image_id = Self::import_buffer(framebuffer, config, image_type, index)
            .await
            .context("Frame::new() import_buffer")?;

        let (signal_event, signal_event_id) = Self::create_and_import_event(framebuffer).await?;
        let (wait_event, wait_event_id) = Self::create_and_import_event(framebuffer).await?;

        Ok(Frame {
            config: *config,
            image_id: image_id,
            image_index: index,
            signal_event,
            signal_event_id,
            wait_event,
            wait_event_id,
            image_vmo,
            mapping: Arc::new(mapping),
        })
    }

    pub fn write_pixel(&mut self, x: u32, y: u32, value: &[u8]) {
        if x < self.config.width && y < self.config.height {
            let pixel_size = self.config.pixel_size_bytes as usize;
            let offset = self.linear_stride_bytes() * y as usize + x as usize * pixel_size;
            self.write_pixel_at_offset(offset, value);
        }
    }

    pub fn write_pixel_at_offset(&mut self, offset: usize, value: &[u8]) {
        self.mapping.write_at(offset, value);
    }

    pub fn fill_rectangle(&mut self, x: u32, y: u32, width: u32, height: u32, value: &[u8]) {
        let left = x.min(self.config.width);
        let right = (left + width).min(self.config.width);
        let top = y.min(self.config.height);
        let bottom = (top + height).min(self.config.width);
        for j in top..bottom {
            for i in left..right {
                self.write_pixel(i, j, value);
            }
        }
    }

    pub fn linear_stride_bytes(&self) -> usize {
        self.config.linear_stride_bytes as usize
    }

    pub fn pixel_size_bytes(&self) -> usize {
        self.config.pixel_size_bytes as usize
    }
}

pub struct VSyncMessage {
    pub display_id: u64,
    pub timestamp: u64,
    pub images: Vec<u64>,
    pub cookie: u64,
}

pub struct FrameBuffer {
    #[allow(unused)]
    display_controller: zx::Channel,
    pub controller: ControllerProxy,
    sysmem: fidl_fuchsia_sysmem::AllocatorProxy,
    pub local_token: Option<fidl_fuchsia_sysmem::BufferCollectionTokenProxy>,
    config: Config,
    layer_id: u64,
    frames: BTreeMap<u64, Frame>,
    #[allow(unused)]
    pub usage: FrameUsage,
}

impl FrameBuffer {
    async fn create_config_from_event_stream(
        stream: &mut fidl_fuchsia_hardware_display::ControllerEventStream,
    ) -> Result<Config, Error> {
        let display_id;
        let pixel_format;
        let width;
        let height;
        let refresh_rate_e2;

        loop {
            let timeout = 2_i64.seconds().after_now();
            let event = stream.next().on_timeout(timeout, || None).await;
            if let Some(event) = event {
                if let Ok(ControllerEvent::OnDisplaysChanged { added, .. }) = event {
                    let first_added = &added[0];
                    display_id = first_added.id;
                    pixel_format = first_added.pixel_format[0];
                    width = first_added.modes[0].horizontal_resolution;
                    height = first_added.modes[0].vertical_resolution;
                    refresh_rate_e2 = first_added.modes[0].refresh_rate_e2;
                    break;
                }
            } else {
                return Err(format_err!(
                    "Timed out waiting for display controller to send a OnDisplaysChanged event"
                ));
            }
        }
        let pixel_size_bytes = pixel_format_bytes(pixel_format) as u32;
        Ok(Config {
            display_id: display_id,
            width: width,
            height: height,
            refresh_rate_e2: refresh_rate_e2,
            linear_stride_bytes: width * pixel_size_bytes,
            format: pixel_format.into(),
            pixel_size_bytes: pixel_size_bytes,
        })
    }

    async fn configure_layer(
        config: &Config,
        image_type: u32,
        proxy: &ControllerProxy,
    ) -> Result<u64, Error> {
        let (status, layer_id) = proxy.create_layer().await?;
        if status != zx::sys::ZX_OK {
            return Err(format_err!("Failed to create layer {}", Status::from_raw(status)));
        }
        let mut image_config = Frame::create_image_config(image_type, config);
        proxy.set_layer_primary_config(layer_id, &mut image_config)?;

        proxy.set_display_layers(config.display_id, &[layer_id])?;
        Ok(layer_id)
    }

    async fn import_buffer_collection(
        &mut self,
        buffer_collection_id: u64,
        buffer_collection: endpoints::ClientEnd<fidl_fuchsia_sysmem::BufferCollectionTokenMarker>,
    ) -> Result<(), Error> {
        self.controller.import_buffer_collection(buffer_collection_id, buffer_collection).await?;
        Ok(())
    }

    async fn set_buffer_collection_constraints(
        &mut self,
        buffer_collection_id: u64,
        config: &Config,
    ) -> Result<(), Error> {
        let mut image_config = Frame::create_image_config(0, config);
        self.controller
            .set_buffer_collection_constraints(buffer_collection_id, &mut image_config)
            .await?;

        Ok(())
    }

    async fn allocate_buffer_collection(
        &mut self,
        frame_count: usize,
        config: &Config,
    ) -> Result<fidl_fuchsia_sysmem::BufferCollectionProxy, Error> {
        let local_token = self.local_token.take().expect("token in allocate_buffer_collection");
        let (display_token, display_token_request) =
            create_endpoints::<fidl_fuchsia_sysmem::BufferCollectionTokenMarker>()?;

        // Duplicate local token for display.
        local_token.duplicate(std::u32::MAX, display_token_request)?;

        // Ensure that duplicate message has been processed by sysmem.
        local_token.sync().await?;

        // Frame buffer import of buffer collection.
        self.import_buffer_collection(BUFFER_COLLECTION_ID, display_token).await?;
        self.set_buffer_collection_constraints(BUFFER_COLLECTION_ID, config).await?;

        //  Bind and set local buffer collection constraints.
        let (collection_client, collection_request) =
            create_endpoints::<fidl_fuchsia_sysmem::BufferCollectionMarker>()?;
        self.sysmem.bind_shared_collection(
            ClientEnd::new(local_token.into_channel().unwrap().into_zx_channel()),
            collection_request,
        )?;
        let collection_client = collection_client.into_proxy()?;
        let pixel_format: fidl_fuchsia_sysmem::PixelFormatType = config.format.into();
        let mut buffer_collection_constraints = crate::sysmem::buffer_collection_constraints(
            config.width,
            config.height,
            pixel_format,
            frame_count as u32,
            self.usage,
        );
        collection_client
            .set_constraints(true, &mut buffer_collection_constraints)
            .context("Sending buffer constraints to sysmem")?;

        Ok(collection_client)
    }

    pub async fn new(
        usage: FrameUsage,
        display_index: Option<usize>,
        vsync_sender: Option<futures::channel::mpsc::UnboundedSender<VSyncMessage>>,
    ) -> Result<FrameBuffer, Error> {
        let device_path = if let Some(index) = display_index {
            format!("/dev/class/display-controller/{:03}", index)
        } else {
            // If the caller did not supply a display index, we watch the
            // display-controller and use the first display that appears.
            let mut first_path = None;
            let dir = OpenOptions::new().read(true).open("/dev/class/display-controller")?;
            watch_directory(&dir, ZX_TIME_INFINITE, |_event, path| {
                first_path = Some(format!("/dev/class/display-controller/{}", path.display()));
                Err(zx::Status::STOP)
            });
            first_path.unwrap()
        };
        let file = OpenOptions::new().read(true).write(true).open(device_path)?;

        let channel = fdio::clone_channel(&file)?;
        let mut provider = ProviderSynchronousProxy::new(channel);

        let (device_client, device_server) = zx::Channel::create()?;
        let (dc_client, dc_server) = endpoints::create_endpoints::<ControllerMarker>()?;
        let status = provider.open_controller(device_server, dc_server, zx::Time::INFINITE)?;
        if status != zx::sys::ZX_OK {
            return Err(format_err!("Failed to open display controller"));
        }

        let proxy = dc_client.into_proxy()?;
        FrameBuffer::new_with_proxy(usage, proxy, device_client, vsync_sender).await
    }

    async fn new_with_proxy(
        usage: FrameUsage,
        proxy: ControllerProxy,
        device_client: zx::Channel,
        vsync_sender: Option<futures::channel::mpsc::UnboundedSender<VSyncMessage>>,
    ) -> Result<FrameBuffer, Error> {
        let mut stream = proxy.take_event_stream();
        let config = Self::create_config_from_event_stream(&mut stream).await?;

        if let Some(vsync_sender) = vsync_sender {
            proxy.enable_vsync(true).context("enable_vsync failed")?;
            fasync::Task::local(
                stream
                    .map_ok(move |request| match request {
                        ControllerEvent::OnVsync { display_id, timestamp, images, cookie } => {
                            vsync_sender
                                .unbounded_send(VSyncMessage {
                                    display_id,
                                    timestamp,
                                    images,
                                    cookie,
                                })
                                .unwrap_or_else(|e| eprintln!("{:?}", e));
                        }
                        _ => (),
                    })
                    .try_collect::<()>()
                    .unwrap_or_else(|e| eprintln!("view listener error: {:?}", e)),
            )
            .detach();
        }

        // Connect to sysmem and allocate shared buffer collection.
        let sysmem = connect_to_service::<fidl_fuchsia_sysmem::AllocatorMarker>()?;

        let (local_token, local_token_request) =
            create_endpoints::<fidl_fuchsia_sysmem::BufferCollectionTokenMarker>()?;
        let local_token = local_token.into_proxy()?;

        sysmem.allocate_shared_collection(local_token_request)?;

        let fb = FrameBuffer {
            display_controller: device_client,
            controller: proxy,
            sysmem,
            local_token: Some(local_token),
            config,
            layer_id: 0,
            frames: BTreeMap::new(),
            usage,
        };

        Ok(fb)
    }

    pub async fn allocate_frames(
        &mut self,
        frame_count: usize,
        format: PixelFormat,
    ) -> Result<(), Error> {
        let pixel_size_bytes = pixel_format_bytes(format.into()) as u32;
        let mut config = Config {
            display_id: self.config.display_id,
            width: self.config.width,
            height: self.config.height,
            refresh_rate_e2: self.config.refresh_rate_e2,
            linear_stride_bytes: self.config.width * pixel_size_bytes,
            format: format.into(),
            pixel_size_bytes: pixel_size_bytes,
        };
        let collection_proxy = self.allocate_buffer_collection(frame_count, &config).await?;

        let (status, buffers) = collection_proxy.wait_for_buffers_allocated().await?;
        if status != zx::sys::ZX_OK {
            return Err(format_err!(
                "Failed to wait for buffers {}({})",
                Status::from_raw(status),
                status
            ));
        }

        if !buffers.settings.has_image_format_constraints {
            return Err(format_err!("No image format constraints"));
        }

        let image_type =
            if buffers.settings.image_format_constraints.pixel_format.has_format_modifier {
                match buffers.settings.image_format_constraints.pixel_format.format_modifier.value {
                    fidl_fuchsia_sysmem::FORMAT_MODIFIER_INTEL_I915_X_TILED => 1,
                    _ => 0,
                }
            } else {
                0
            };

        config.linear_stride_bytes = crate::sysmem::minimum_row_bytes(
            buffers.settings.image_format_constraints,
            self.config.width,
        )?;
        self.config.linear_stride_bytes = config.linear_stride_bytes;
        self.layer_id = Self::configure_layer(&config, image_type, &self.controller).await?;

        // Clean close of collection in order to allow other clients to
        // continue usage.
        collection_proxy.close()?;

        for index in 0..frame_count {
            let vmo_buffer = &buffers.buffers[index];
            let vmo = vmo_buffer
                .vmo
                .as_ref()
                .expect("vmo_buffer")
                .duplicate_handle(zx::Rights::SAME_RIGHTS)
                .context("duplicating buffer vmo")?;
            let frame = Frame::new(self, vmo, &config, image_type, index as u32)
                .await
                .context("new_with_vmo")?;
            self.frames.insert(frame.image_id, frame);
        }
        Ok(())
    }

    pub fn get_config(&self) -> Config {
        self.config
    }

    pub fn byte_size(&self) -> usize {
        self.config.height as usize * self.config.linear_stride_bytes()
    }

    pub fn get_frame_count(&self) -> usize {
        self.frames.len()
    }

    pub fn get_frame(&self, image_id: u64) -> &Frame {
        self.frames.get(&image_id).expect("to find image")
    }

    pub fn get_frame_mut(&mut self, image_id: u64) -> &mut Frame {
        self.frames.get_mut(&image_id).expect("to find image")
    }

    pub fn get_image_ids(&self) -> BTreeSet<u64> {
        self.frames.keys().map(|image_id| *image_id).collect::<BTreeSet<_>>()
    }

    pub fn get_first_frame_id(&self) -> u64 {
        if let Some(image_id) = self.get_image_ids().iter().next() {
            *image_id
        } else {
            // this should be impossible, as a frame buffer cannot be
            // created with zero frames
            panic!("no allocated frames")
        }
    }

    pub fn flush_frame(&mut self, image_id: u64) -> Result<(), Error> {
        let frame = self.get_frame(image_id);
        frame.image_vmo.op_range(VmoOp::CACHE_CLEAN_INVALIDATE, 0, frame.image_vmo.get_size()?)?;
        Ok(())
    }

    pub fn present_frame(
        &mut self,
        image_id: u64,
        sender: Option<futures::channel::mpsc::UnboundedSender<u64>>,
        signal_wait_event: bool,
    ) -> Result<(), Error> {
        let frame = self.get_frame(image_id);
        self.controller.set_display_layers(self.config.display_id, &[self.layer_id])?;
        self.controller
            .set_layer_image(
                self.layer_id,
                frame.image_id,
                frame.wait_event_id,
                frame.signal_event_id,
            )
            .context("Frame::present() set_layer_image")?;
        self.controller.apply_config().context("Frame::present() apply_config")?;
        if signal_wait_event {
            frame.wait_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;
        }
        if let Some(signal_sender) = sender.as_ref() {
            let signal_sender = signal_sender.clone();
            let image_id = frame.image_id;
            let local_event = frame.signal_event.duplicate_handle(zx::Rights::SAME_RIGHTS)?;
            local_event.as_handle_ref().signal(Signals::EVENT_SIGNALED, Signals::NONE)?;
            fasync::Task::local(async move {
                let signals = OnSignals::new(&local_event, Signals::EVENT_SIGNALED);
                signals.await.expect("to wait");
                signal_sender.unbounded_send(image_id).expect("send to work");
            })
            .detach();
        }
        Ok(())
    }

    pub fn present_first_frame(
        &mut self,
        sender: Option<futures::channel::mpsc::UnboundedSender<u64>>,
        signal_wait_event: bool,
    ) -> Result<(), Error> {
        self.present_frame(self.get_first_frame_id(), sender, signal_wait_event)
    }

    pub fn acknowledge_vsync(&mut self, cookie: u64) -> Result<(), Error> {
        self.controller.acknowledge_vsync(cookie)?;
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_hardware_display::ControllerRequest;
    use std::cell::Cell;
    use std::rc::Rc;

    #[fasync::run_singlethreaded(test)]
    async fn test_no_vsync() -> Result<(), anyhow::Error> {
        let (client, server) = fidl::endpoints::create_endpoints::<ControllerMarker>()?;
        let mut stream = server.into_stream()?;
        let vsync_enabled = Rc::new(Cell::new(false));
        let vsync_enabled_clone = Rc::clone(&vsync_enabled);

        fasync::Task::local(async move {
            while let Some(req) = stream.try_next().await.expect("Failed to get request!") {
                match req {
                    ControllerRequest::EnableVsync { enable: true, control_handle: _ } => {
                        vsync_enabled_clone.set(true)
                    }
                    ControllerRequest::EnableVsync { enable: false, control_handle: _ } => {
                        vsync_enabled_clone.set(false)
                    }
                    _ => panic!("Unexpected request"),
                }
            }
        })
        .detach();

        let proxy = client.into_proxy()?;
        let (dummy, _) = zx::Channel::create()?;
        let _fb = FrameBuffer::new_with_proxy(FrameUsage::Cpu, proxy, dummy, None);
        if vsync_enabled.get() {
            panic!("Vsync should be disabled");
        }

        Ok(())
    }
}
