// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{ensure, format_err, Context as _, Error};
use fidl::endpoints::{self, ClientEnd};
use fidl_fuchsia_hardware_display::{
    ConfigStamp, ControllerEvent, ControllerMarker, ControllerProxy, ImageConfig,
    ProviderSynchronousProxy, VirtconMode,
};
use fidl_fuchsia_sysmem::{ImageFormatConstraints, PixelFormatType};
use fuchsia_async::{self as fasync, DurationExt, OnSignals, TimeoutExt};
use fuchsia_zircon::{
    self as zx, AsHandleRef, DurationNum, Event, HandleBased, Signals, Status, Vmo, VmoOp,
};
use futures::{StreamExt, TryFutureExt, TryStreamExt};
use mapped_vmo::Mapping;
use std::{
    collections::{BTreeMap, BTreeSet},
    sync::Arc,
};

#[cfg(test)]
use std::ops::Range;

pub mod sysmem;

use sysmem::BufferCollectionAllocator;

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
pub type CollectionId = u64;

pub struct ImageInCollection {
    pub image_id: ImageId,
    pub collection_id: CollectionId,
}

#[derive(Debug)]
pub struct FrameSet {
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    collection_id: CollectionId,
    image_count: usize,
    available: BTreeSet<ImageId>,
    pub prepared: Option<ImageId>,
    presented: BTreeSet<ImageId>,
}

impl FrameSet {
    pub fn new(collection_id: CollectionId, available: BTreeSet<ImageId>) -> FrameSet {
        FrameSet {
            collection_id,
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
        Self::new(0, available)
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
    pub horizontal_size_mm: u32,
    pub vertical_size_mm: u32,
    pub using_fallback_size: bool,
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

#[derive(Debug)]
pub struct Frame {
    config: Config,
    pub image_id: u64,
    pub collection_id: u64,
    pub image_index: u32,
    pub signal_event: Event,
    signal_event_id: u64,
    pub wait_event: Event,
    wait_event_id: u64,
    image_vmo: Option<Vmo>,
    pub mapping: Option<Arc<Mapping>>,
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
        collection_id: u64,
    ) -> Result<u64, Error> {
        let mut image_config = Self::create_image_config(image_type, config);

        let (status, image_id) = framebuffer
            .controller
            .import_image(&mut image_config, collection_id, index)
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
        image_vmo: Option<Vmo>,
        config: &Config,
        image_type: u32,
        index: u32,
        collection_id: u64,
    ) -> Result<Frame, Error> {
        let mapping: Option<Arc<Mapping>> = image_vmo.as_ref().and_then(|image_vmo| {
            Some(Arc::new(
                Mapping::create_from_vmo(
                    &image_vmo,
                    framebuffer.byte_size(),
                    zx::VmarFlags::PERM_READ
                        | zx::VmarFlags::PERM_WRITE
                        | zx::VmarFlags::MAP_RANGE
                        | zx::VmarFlags::REQUIRE_NON_RESIZABLE,
                )
                .expect("Frame::new() Mapping::create_from_vmo failed"),
            ))
        });

        // import image
        let image_id = Self::import_buffer(framebuffer, config, image_type, index, collection_id)
            .await
            .context("Frame::new() import_buffer")?;

        let (signal_event, signal_event_id) = Self::create_and_import_event(framebuffer).await?;
        let (wait_event, wait_event_id) = Self::create_and_import_event(framebuffer).await?;

        Ok(Frame {
            config: *config,
            image_id: image_id,
            collection_id,
            image_index: index,
            signal_event,
            signal_event_id,
            wait_event,
            wait_event_id,
            image_vmo,
            mapping,
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
        self.mapping.as_mut().expect("mapping").write_at(offset, value);
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

    pub fn flush(&self) -> Result<(), Error> {
        if let Some(image_vmo) = self.image_vmo.as_ref() {
            image_vmo.op_range(VmoOp::CACHE_CLEAN_INVALIDATE, 0, image_vmo.get_size()?)?;
        }
        Ok(())
    }

    pub fn linear_stride_bytes(&self) -> usize {
        self.config.linear_stride_bytes as usize
    }

    pub fn pixel_size_bytes(&self) -> usize {
        self.config.pixel_size_bytes as usize
    }
}

#[derive(Debug)]
pub struct FrameCollection {
    pub frames: BTreeMap<u64, Frame>,
    pub usage: FrameUsage,
    pub collection_id: u64,
    pub image_format_constraints: ImageFormatConstraints,
}

impl FrameCollection {
    pub fn new(
        collection_id: u64,
        frames: BTreeMap<u64, Frame>,
        usage: FrameUsage,
        image_format_constraints: ImageFormatConstraints,
    ) -> Self {
        Self { frames, usage, collection_id, image_format_constraints }
    }

    pub fn release(self, framebuffer: &mut FrameBuffer) {
        framebuffer
            .release_buffer_collection(self.collection_id)
            .unwrap_or_else(|e| eprintln!("{:?}", e));
    }

    pub fn get_image_ids(&self) -> BTreeSet<u64> {
        self.frames.keys().map(|image_id| *image_id).collect::<BTreeSet<_>>()
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

    pub fn get_first_frame_id(&self) -> u64 {
        if let Some(image_id) = self.get_image_ids().iter().next() {
            *image_id
        } else {
            // this should be impossible, as a frame buffer cannot be
            // created with zero frames
            panic!("no allocated frames")
        }
    }

    pub fn image_type(&self) -> u32 {
        if self.image_format_constraints.pixel_format.has_format_modifier {
            match self.image_format_constraints.pixel_format.format_modifier.value {
                fidl_fuchsia_sysmem::FORMAT_MODIFIER_INTEL_I915_X_TILED => 1,
                fidl_fuchsia_sysmem::FORMAT_MODIFIER_INTEL_I915_Y_TILED => 2,
                _ => 0,
            }
        } else {
            0
        }
    }
}

pub struct FrameCollectionBuilder {
    frame_count: usize,
    usage: FrameUsage,
    allocator: BufferCollectionAllocator,
}

impl FrameCollectionBuilder {
    pub fn new(
        width: u32,
        height: u32,
        pixel_type: PixelFormatType,
        usage: FrameUsage,
        frame_count: usize,
    ) -> Result<Self, Error> {
        Ok(Self {
            frame_count,
            usage,
            allocator: BufferCollectionAllocator::new(
                width,
                height,
                pixel_type,
                usage,
                frame_count,
            )?,
        })
    }

    pub fn set_pixel_type(&mut self, pixel_type: PixelFormatType) {
        self.allocator.set_pixel_type(pixel_type);
    }

    pub async fn build(
        mut self,
        collection_id: u64,
        config: &Config,
        set_constraints: bool,
        framebuffer: &mut FrameBuffer,
    ) -> Result<FrameCollection, Error> {
        let display_token = self.allocator.duplicate_token().await?;

        framebuffer.import_buffer_collection(collection_id, display_token).await?;
        framebuffer.set_buffer_collection_constraints(collection_id, 0, config).await?;

        let buffers = self.allocator.allocate_buffers(set_constraints).await?;

        ensure!(buffers.settings.has_image_format_constraints, "No image format constraints");
        ensure!(
            buffers.buffer_count as usize == self.frame_count,
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

        let mut frames = BTreeMap::new();
        for index in 0..self.frame_count {
            let vmo_buffer = &buffers.buffers[index];
            let vmo = vmo_buffer.vmo.as_ref().and_then(|vmo| {
                Some(vmo.duplicate_handle(zx::Rights::SAME_RIGHTS).expect("duplicate_handle"))
            });
            let frame =
                Frame::new(framebuffer, vmo, &config, image_type, index as u32, collection_id)
                    .await
                    .context("new_with_vmo")?;
            frames.insert(frame.image_id, frame);
        }

        Ok(FrameCollection::new(
            collection_id,
            frames,
            self.usage,
            buffers.settings.image_format_constraints,
        ))
    }

    pub async fn duplicate_token(
        &mut self,
    ) -> Result<ClientEnd<fidl_fuchsia_sysmem::BufferCollectionTokenMarker>, Error> {
        self.allocator.duplicate_token().await
    }
}

#[cfg(test)]
mod frame_collection_tests {
    use super::*;

    const BUFFER_COLLECTION_ID: u64 = 1;

    #[fasync::run_singlethreaded(test)]
    #[ignore]
    async fn test_build_frame_collection() -> Result<(), anyhow::Error> {
        const FRAME_COUNT: usize = 2;
        let mut fb = FrameBuffer::new(FrameUsage::Cpu, None, None, None).await?;
        let config = fb.get_config();
        let mut frame_collection_builder = FrameCollectionBuilder::new(
            config.width,
            config.height,
            config.format.into(),
            FrameUsage::Cpu,
            FRAME_COUNT,
        )?;
        let another = frame_collection_builder.duplicate_token().await?;
        another.into_proxy()?.close()?;
        let frame_collection =
            frame_collection_builder.build(BUFFER_COLLECTION_ID, &config, true, &mut fb).await?;
        assert_eq!(frame_collection.collection_id, BUFFER_COLLECTION_ID);
        assert_eq!(frame_collection.frames.len(), FRAME_COUNT);

        Ok(())
    }
}

#[derive(Debug)]
pub struct VSync {
    pub display_id: u64,
    pub timestamp: u64,
    pub applied_config_stamp: ConfigStamp,
    pub cookie: u64,
}

#[derive(Debug)]
pub enum Message {
    VSync(VSync),
    Ownership(bool),
}

pub struct FrameBuffer {
    pub controller: ControllerProxy,
    config: Config,
    layer_id: u64,
    pub usage: FrameUsage,
    initial_virtcon_mode: Option<VirtconMode>,
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
        let horizontal_size_mm;
        let vertical_size_mm;
        let using_fallback_size;

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
                    horizontal_size_mm = first_added.horizontal_size_mm;
                    vertical_size_mm = first_added.vertical_size_mm;
                    using_fallback_size = first_added.using_fallback_size;
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
            horizontal_size_mm: horizontal_size_mm,
            vertical_size_mm: vertical_size_mm,
            using_fallback_size,
            linear_stride_bytes: width * pixel_size_bytes,
            format: pixel_format.into(),
            pixel_size_bytes: pixel_size_bytes,
        })
    }

    async fn create_layer(&mut self) -> Result<u64, Error> {
        let (status, layer_id) = self.controller.create_layer().await?;
        ensure!(status == zx::sys::ZX_OK, "Failed to create layer {}", Status::from_raw(status));
        Ok(layer_id)
    }

    pub fn configure_layer(&mut self, config: &Config, image_type: u32) -> Result<(), Error> {
        let mut image_config = Frame::create_image_config(image_type, config);
        self.controller.set_layer_primary_config(self.layer_id, &mut image_config)?;
        self.controller.set_display_layers(config.display_id, &[self.layer_id])?;
        Ok(())
    }

    pub(crate) async fn import_buffer_collection(
        &mut self,
        buffer_collection_id: u64,
        buffer_collection: endpoints::ClientEnd<fidl_fuchsia_sysmem::BufferCollectionTokenMarker>,
    ) -> Result<(), Error> {
        self.controller.import_buffer_collection(buffer_collection_id, buffer_collection).await?;
        Ok(())
    }

    pub(crate) fn release_buffer_collection(
        &mut self,
        buffer_collection_id: u64,
    ) -> Result<(), Error> {
        self.controller.release_buffer_collection(buffer_collection_id)?;
        Ok(())
    }

    pub(crate) async fn set_buffer_collection_constraints(
        &mut self,
        buffer_collection_id: u64,
        image_type: u32,
        config: &Config,
    ) -> Result<(), Error> {
        let mut image_config = Frame::create_image_config(image_type, config);
        self.controller
            .set_buffer_collection_constraints(buffer_collection_id, &mut image_config)
            .await?;

        Ok(())
    }

    pub async fn new(
        usage: FrameUsage,
        virtcon_mode: Option<VirtconMode>,
        display_index: Option<usize>,
        sender: Option<futures::channel::mpsc::UnboundedSender<Message>>,
    ) -> Result<FrameBuffer, Error> {
        let device_path = if let Some(index) = display_index {
            format!("/dev/class/display-controller/{:03}", index)
        } else {
            // If the caller did not supply a display index, we watch the
            // display-controller directory and use the first controller
            // that appears.
            let dir = fuchsia_fs::directory::open_in_namespace(
                "/dev/class/display-controller",
                fuchsia_fs::OpenFlags::RIGHT_READABLE,
            )?;

            let timeout = 10.seconds().after_now();
            let watcher = fuchsia_vfs_watcher::Watcher::new(dir).await?;
            let filename = watcher
                .try_filter_map(|fuchsia_vfs_watcher::WatchMessage { event, filename }| {
                    futures::future::ok(match event {
                        fuchsia_vfs_watcher::WatchEvent::ADD_FILE
                        | fuchsia_vfs_watcher::WatchEvent::EXISTING => Some(filename),
                        _ => None,
                    })
                })
                .next()
                .on_timeout(timeout, || None)
                .await;
            let filename =
                filename.ok_or_else(|| format_err!("No display controller available"))?;
            let filename = filename?;
            format!("/dev/class/display-controller/{}", filename.display())
        };

        let (client_end, server_end) = zx::Channel::create()?;
        fuchsia_component::client::connect_channel_to_protocol_at_path(server_end, &device_path)?;
        let provider = ProviderSynchronousProxy::new(client_end);

        let (dc_client, dc_server) = endpoints::create_endpoints::<ControllerMarker>()?;
        let status = if virtcon_mode.is_some() {
            provider.open_virtcon_controller(dc_server, zx::Time::INFINITE)
        } else {
            provider.open_controller(dc_server, zx::Time::INFINITE)
        }?;
        let () = zx::Status::ok(status)?;

        let proxy = dc_client.into_proxy()?;
        if let Some(virtcon_mode) = virtcon_mode {
            proxy.set_virtcon_mode(virtcon_mode as u8)?;
        }
        FrameBuffer::new_with_proxy(virtcon_mode, usage, proxy, sender).await
    }

    async fn new_with_proxy(
        initial_virtcon_mode: Option<VirtconMode>,
        usage: FrameUsage,
        proxy: ControllerProxy,
        sender: Option<futures::channel::mpsc::UnboundedSender<Message>>,
    ) -> Result<FrameBuffer, Error> {
        let mut stream = proxy.take_event_stream();
        let config = Self::create_config_from_event_stream(&mut stream).await?;

        if let Some(sender) = sender {
            proxy.enable_vsync(true).context("enable_vsync failed")?;
            fasync::Task::local(
                stream
                    .map_ok(move |request| match request {
                        ControllerEvent::OnVsync {
                            display_id,
                            timestamp,
                            applied_config_stamp,
                            cookie,
                        } => {
                            sender
                                .unbounded_send(Message::VSync(VSync {
                                    display_id,
                                    timestamp,
                                    applied_config_stamp,
                                    cookie,
                                }))
                                .unwrap_or_else(|e| eprintln!("{:?}", e));
                        }
                        ControllerEvent::OnClientOwnershipChange { has_ownership } => {
                            sender
                                .unbounded_send(Message::Ownership(has_ownership))
                                .unwrap_or_else(|e| eprintln!("{:?}", e));
                        }
                        _ => (),
                    })
                    .try_collect::<()>()
                    .unwrap_or_else(|e| eprintln!("view listener error: {:?}", e)),
            )
            .detach();
        }

        let mut fb =
            FrameBuffer { initial_virtcon_mode, controller: proxy, config, layer_id: 0, usage };

        let layer_id = fb.create_layer().await?;
        fb.layer_id = layer_id;

        Ok(fb)
    }

    pub fn set_virtcon_mode(&mut self, virtcon_mode: VirtconMode) -> Result<(), Error> {
        ensure!(self.initial_virtcon_mode.is_some());
        self.controller.set_virtcon_mode(virtcon_mode as u8)?;
        Ok(())
    }

    pub fn get_config(&self) -> Config {
        self.config
    }

    pub fn get_config_for_format(&self, format: PixelFormat) -> Config {
        let pixel_size_bytes = pixel_format_bytes(format.into()) as u32;
        Config {
            display_id: self.config.display_id,
            width: self.config.width,
            height: self.config.height,
            refresh_rate_e2: self.config.refresh_rate_e2,
            horizontal_size_mm: self.config.horizontal_size_mm,
            vertical_size_mm: self.config.vertical_size_mm,
            using_fallback_size: self.config.using_fallback_size,
            linear_stride_bytes: self.config.width * pixel_size_bytes,
            format: format.into(),
            pixel_size_bytes: pixel_size_bytes,
        }
    }

    pub fn byte_size(&self) -> usize {
        self.config.height as usize * self.config.linear_stride_bytes()
    }

    pub fn present_frame(
        &mut self,
        frame: &Frame,
        sender: Option<futures::channel::mpsc::UnboundedSender<ImageInCollection>>,
        signal_wait_event: bool,
    ) -> Result<(), Error> {
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
            let collection_id = frame.collection_id;
            let local_event = frame.signal_event.duplicate_handle(zx::Rights::SAME_RIGHTS)?;
            local_event.as_handle_ref().signal(Signals::EVENT_SIGNALED, Signals::NONE)?;
            fasync::Task::local(async move {
                let signals = OnSignals::new(&local_event, Signals::EVENT_SIGNALED);
                signals.await.expect("to wait");
                signal_sender
                    .unbounded_send(ImageInCollection { image_id, collection_id })
                    .expect("send to work");
            })
            .detach();
        }
        Ok(())
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
        let _fb = FrameBuffer::new_with_proxy(None, FrameUsage::Cpu, proxy, None);
        if vsync_enabled.get() {
            panic!("Vsync should be disabled");
        }

        Ok(())
    }
}
