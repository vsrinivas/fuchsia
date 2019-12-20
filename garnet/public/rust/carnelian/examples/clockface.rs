// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    carnelian::{
        make_app_assistant, AnimationMode, App, AppAssistant, FrameBufferPtr, Point, Size,
        ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey, ViewMode,
    },
    chrono::{Local, Timelike},
    euclid::{Angle, Transform2D, Vector2D},
    failure::Error,
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_sysmem::BufferCollectionTokenMarker,
    std::{cell::RefCell, collections::BTreeMap, env, f32, rc::Rc},
};

mod spinel_utils;

use crate::spinel_utils::{
    Context, GroupId, MoldContext, Path, PathBuilder, Raster, RasterBuilder, SpinelContext,
};

const APP_NAME: &'static [u8; 13usize] = b"clockface_rs\0";
const BACKGROUND_COLOR: [f32; 4] = [0.922, 0.835, 0.702, 1.0];

#[derive(Default)]
struct ClockfaceAppAssistant;

impl AppAssistant for ClockfaceAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant_image_pipe(
        &mut self,
        _: ViewKey,
        fb: FrameBufferPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        let (token, token_request) =
            create_endpoints::<BufferCollectionTokenMarker>().expect("create_endpoint");
        fb.borrow()
            .local_token
            .as_ref()
            .unwrap()
            .duplicate(std::u32::MAX, token_request)
            .expect("duplicate");

        let config = &fb.borrow().get_config();

        if env::args().any(|v| v == "--mold") {
            Ok(Box::new(ClockfaceViewAssistant::new(MoldContext::new(token, config))))
        } else {
            const BLOCK_POOL_SIZE: u64 = 1 << 22; // 4 MB
            const HANDLE_COUNT: u32 = 1 << 10; // 1K handles
            const LAYERS_COUNT: u32 = 32;
            const CMDS_COUNT: u32 = 256;

            Ok(Box::new(ClockfaceViewAssistant::new(SpinelContext::new(
                token,
                config,
                APP_NAME.as_ptr(),
                BLOCK_POOL_SIZE,
                HANDLE_COUNT,
                LAYERS_COUNT,
                CMDS_COUNT,
            ))))
        }
    }

    fn get_mode(&self) -> ViewMode {
        ViewMode::ImagePipe
    }
}

fn line(path_builder: &mut dyn PathBuilder, p0: Point, p1: Point) {
    path_builder.move_to(&p0);
    path_builder.line_to(&p1);
}

fn lerp(t: f32, p0: Point, p1: Point) -> Point {
    Point::new(p0.x * (1.0 - t) + p1.x * t, p0.y * (1.0 - t) + p1.y * t)
}

// TODO: Remove and use spn_path_builder_cubic_to.
fn cubic(path_builder: &mut dyn PathBuilder, p0: Point, p1: Point, p2: Point, p3: Point) {
    // Adjust if resolution is expected to be significantly different.
    const RESOLUTION: f32 = 1024.0;
    let deviation_x = (p0.x + p2.x - 3.0 * (p1.x + p2.x)).abs() * RESOLUTION;
    let deviation_y = (p0.y + p2.y - 3.0 * (p1.y + p2.y)).abs() * RESOLUTION;
    let deviation_squared = deviation_x * deviation_x + deviation_y * deviation_y;
    const PIXEL_ACCURACY: f32 = 0.25;
    if deviation_squared < PIXEL_ACCURACY {
        line(path_builder, p0, p3);
        return;
    }
    const TOLERANCE: f32 = 3.0;
    let subdivisions = 1 + (TOLERANCE * deviation_squared).sqrt().sqrt().floor() as usize;
    let increment = (subdivisions as f32).recip();
    let mut t = 0.0;
    path_builder.move_to(&p0);
    for _ in 0..subdivisions - 1 {
        t += increment;
        let p_next = lerp(
            t,
            lerp(t, lerp(t, p0, p1), lerp(t, p1, p2)),
            lerp(t, lerp(t, p1, p2), lerp(t, p2, p3)),
        );
        path_builder.line_to(&p_next);
    }
    path_builder.line_to(&p3);
}

struct RoundedLine {
    path: Path,
}

impl RoundedLine {
    fn new(path_builder: &mut dyn PathBuilder, pos: Point, length: f32, thinkness: f32) -> Self {
        let dist = 4.0 / 3.0 * (f32::consts::PI / 8.0).tan();
        let radius = thinkness / 2.0;
        let control_dist = dist * radius;
        let tl = pos.to_vector();
        let tr = pos.to_vector() + Point::new(length, 0.0).to_vector();
        let br = pos.to_vector() + Point::new(length, thinkness).to_vector();
        let bl = pos.to_vector() + Point::new(0.0, thinkness).to_vector();
        let rt = Point::new(0.0, radius).to_vector();
        let rr = Point::new(-radius, 0.0).to_vector();
        let rb = Point::new(0.0, -radius).to_vector();
        let rl = Point::new(radius, 0.0).to_vector();
        let ct = Point::new(0.0, -control_dist).to_vector();
        let cr = Point::new(control_dist, 0.0).to_vector();
        let cb = Point::new(0.0, control_dist).to_vector();
        let cl = Point::new(-control_dist, 0.0).to_vector();

        let path = {
            path_builder.begin();

            macro_rules! c {
                ( $v:expr ) => {
                    Point::new($v.x, $v.y)
                };
            }

            line(path_builder, c!(tl + rl), c!(tr + rr));
            cubic(path_builder, c!(tr + rr), c!(tr + rr + cr), c!(tr + rt + ct), c!(tr + rt));
            cubic(path_builder, c!(br + rb), c!(br + rb + cb), c!(br + rr + cr), c!(br + rr));
            line(path_builder, c!(br + rr), c!(bl + rl));
            cubic(path_builder, c!(bl + rl), c!(bl + rl + cl), c!(bl + rb + cb), c!(bl + rb));
            cubic(path_builder, c!(tl + rt), c!(tl + rt + ct), c!(tl + rl + cl), c!(tl + rl));

            path_builder.end()
        };

        Self { path }
    }
}

struct Hand {
    line: RoundedLine,
    elevation: f32,
    layer_id: u32,
    raster: Option<Rc<RefCell<Raster>>>,
    shadow_raster: Option<Rc<RefCell<Raster>>>,
}

impl Hand {
    fn new_raster(
        raster_builder: &mut dyn RasterBuilder,
        path: &Path,
        rotation: &Transform2D<f32>,
        txty: &Vector2D<f32>,
    ) -> Raster {
        let transform = rotation.post_translate(*txty);
        raster_builder.begin();
        const CLIP: [f32; 4] = [std::f32::MIN, std::f32::MIN, std::f32::MAX, std::f32::MAX];
        raster_builder.add(path, &transform, &CLIP);
        raster_builder.end()
    }

    fn new(
        context: &mut dyn Context,
        group_id: &GroupId,
        layer_id: u32,
        thinkness: f32,
        length: f32,
        offset: f32,
        color: &[f32; 4],
        elevation: f32,
    ) -> Self {
        let line = RoundedLine::new(
            context.path_builder(),
            Point::new(-(thinkness / 2.0 + offset), -thinkness / 2.0),
            length,
            thinkness,
        );

        context.styling().group_layer(group_id, layer_id, color);

        Self { line, elevation, layer_id, raster: None, shadow_raster: None }
    }

    fn update(&mut self, context: &mut dyn Context, scale: f32, angle: f32, position: Point) {
        let raster_builder = context.raster_builder();
        let rotation = Transform2D::create_rotation(Angle::radians(angle)).post_scale(scale, scale);

        let txty = Vector2D::new(position.x, position.y);
        let raster = Self::new_raster(raster_builder, &self.line.path, &rotation, &txty);
        self.raster.replace(Rc::new(RefCell::new(raster)));

        let shadow_offset = self.elevation * scale;
        let txty = Vector2D::new(position.x + shadow_offset, position.y + shadow_offset * 2.0);
        let raster = Self::new_raster(raster_builder, &self.line.path, &rotation, &txty);
        self.shadow_raster.replace(Rc::new(RefCell::new(raster)));
    }
}

struct Scene {
    size: Size,
    hour_hand: Hand,
    minute_hand: Hand,
    second_hand: Hand,
    hour_index: usize,
    minute_index: usize,
    second_index: usize,
    shadow_layer_ids: [u32; 2],
    clear_layer_id: u32,
}

impl Scene {
    fn new(context: &mut dyn Context) -> Self {
        const HOUR_HAND_COLOR: [f32; 4] = [0.996, 0.282, 0.392, 1.0];
        const MINUTE_HAND_COLOR: [f32; 4] = [0.996, 0.282, 0.392, 0.5];
        const SECOND_HAND_COLOR: [f32; 4] = [1.0, 1.0, 1.0, 1.0];

        const HOUR_HAND_LAYER_ID: u32 = 0;
        const MINUTE_HAND_LAYER_ID: u32 = HOUR_HAND_LAYER_ID + 1;
        const SECOND_HAND_LAYER_ID: u32 = MINUTE_HAND_LAYER_ID + 1;
        const SHADOW_LAYER_1_ID: u32 = SECOND_HAND_LAYER_ID + 1;
        const SHADOW_LAYER_2_ID: u32 = SHADOW_LAYER_1_ID + 1;
        const CLEAR_LAYER_ID: u32 = SHADOW_LAYER_2_ID + 1;

        let group_id =
            context.styling().alloc_group(HOUR_HAND_LAYER_ID, CLEAR_LAYER_ID, &BACKGROUND_COLOR);
        context.styling().group_layer(&group_id, SHADOW_LAYER_1_ID, &[0.0, 0.0, 0.0, 0.05]);
        context.styling().group_layer(&group_id, SHADOW_LAYER_2_ID, &[0.0, 0.0, 0.0, 0.05]);
        context.styling().group_layer(&group_id, CLEAR_LAYER_ID, &BACKGROUND_COLOR);

        let radius = 0.4;
        let thinkness = radius / 20.0;
        let offset = radius / 5.0;
        let elevation = 0.01;
        let hour_hand = Hand::new(
            context,
            &group_id,
            HOUR_HAND_LAYER_ID,
            thinkness * 2.0,
            radius,
            offset,
            &HOUR_HAND_COLOR,
            elevation,
        );
        let minute_hand = Hand::new(
            context,
            &group_id,
            MINUTE_HAND_LAYER_ID,
            thinkness,
            radius,
            0.0,
            &MINUTE_HAND_COLOR,
            elevation,
        );
        let second_hand = Hand::new(
            context,
            &group_id,
            SECOND_HAND_LAYER_ID,
            thinkness / 2.0,
            radius + offset,
            offset,
            &SECOND_HAND_COLOR,
            elevation,
        );

        context.styling().seal();

        Self {
            size: Size::new(1.0, 1.0),
            hour_hand,
            minute_hand,
            second_hand,
            hour_index: std::usize::MAX,
            minute_index: std::usize::MAX,
            second_index: std::usize::MAX,
            shadow_layer_ids: [SHADOW_LAYER_1_ID, SHADOW_LAYER_2_ID],
            clear_layer_id: CLEAR_LAYER_ID,
        }
    }

    fn update(&mut self, context: &mut dyn Context, size: &Size) {
        if self.size != *size {
            self.size = *size;
            self.hour_index = std::usize::MAX;
            self.minute_index = std::usize::MAX;
            self.second_index = std::usize::MAX;
        }
        let scale = size.width.min(size.height);
        let center = Point::new(size.width / 2.0, size.height / 2.0);
        const MICROSECONDS_PER_SECOND: f32 = 1e+6;
        let now = Local::now();
        let (_is_pm, hour12) = now.hour12();
        let us = now.nanosecond() as f32 / 1000.0;
        let second = now.second() as f32 + us / MICROSECONDS_PER_SECOND;
        let minute = now.minute() as f32 + second / 60.0;
        let hour = hour12 as f32 + minute / 60.0;
        const R0: f32 = -0.25; // Rotate from 3 to 12.
        const STEPS: usize = 60 * 60; // Enough steps to ensure smooth movement
                                      // of second hand each frame on a 60hz display.
        let index = ((R0 + hour / 12.0).rem_euclid(1.0) * STEPS as f32) as usize;
        if index != self.hour_index {
            let angle = index as f32 * 2.0 * f32::consts::PI / STEPS as f32;
            self.hour_hand.update(context, scale, -angle, center);
            self.hour_index = index;
        }
        let index = ((R0 + minute / 60.0).rem_euclid(1.0) * STEPS as f32) as usize;
        if index != self.minute_index {
            let angle = index as f32 * 2.0 * f32::consts::PI / STEPS as f32;
            self.minute_hand.update(context, scale, -angle, center);
            self.minute_index = index;
        }
        let index = ((R0 + second / 60.0).rem_euclid(1.0) * STEPS as f32) as usize;
        if index != self.second_index {
            let angle = index as f32 * 2.0 * f32::consts::PI / STEPS as f32;
            self.second_hand.update(context, scale, -angle, center);
            self.second_index = index;
        }
    }
}

struct Contents {
    index: u32,
    size: Size,
    previous_rasters: Vec<Rc<RefCell<Raster>>>,
}

impl Contents {
    fn new(index: u32) -> Self {
        Self { index, size: Size::zero(), previous_rasters: Vec::new() }
    }

    fn update(&mut self, context: &mut dyn Context, scene: &Scene, size: &Size) {
        let composition = context.composition(self.index);
        composition.unseal();
        composition.reset();

        let mut needs_clear = false;
        if self.size != *size {
            self.size = *size;
            let clip: [u32; 4] = [0, 0, size.width.floor() as u32, size.height.floor() as u32];
            composition.set_clip(&clip);
            needs_clear = true;
        }

        // New rasters for this frame.
        let hour_hand_raster = scene.hour_hand.raster.as_ref().unwrap().clone();
        let hour_hand_shadow_raster = scene.hour_hand.shadow_raster.as_ref().unwrap().clone();
        let minute_hand_raster = scene.minute_hand.raster.as_ref().unwrap().clone();
        let minute_hand_shadow_raster = scene.minute_hand.shadow_raster.as_ref().unwrap().clone();
        let second_hand_raster = scene.second_hand.raster.as_ref().unwrap().clone();
        let second_hand_shadow_raster = scene.second_hand.shadow_raster.as_ref().unwrap().clone();

        // Place new rasters.
        composition.place(&hour_hand_raster.borrow(), scene.hour_hand.layer_id);
        composition.place(&minute_hand_raster.borrow(), scene.minute_hand.layer_id);
        composition.place(&second_hand_raster.borrow(), scene.second_hand.layer_id);

        // Shadow layers.
        // TODO: use txty when supported.
        composition.place(&hour_hand_shadow_raster.borrow(), scene.shadow_layer_ids[0]);
        composition.place(&minute_hand_shadow_raster.borrow(), scene.shadow_layer_ids[0]);
        composition.place(&second_hand_shadow_raster.borrow(), scene.shadow_layer_ids[0]);
        // Minute and is translucent and left out of second shadow layer in order to
        // have it cast a lighter shadow.
        composition.place(&hour_hand_shadow_raster.borrow(), scene.shadow_layer_ids[1]);
        composition.place(&second_hand_shadow_raster.borrow(), scene.shadow_layer_ids[1]);

        // Place previous rasters in clear layer.
        // TODO: Replace with better partial update system.
        for raster in self.previous_rasters.drain(..) {
            composition.place(&raster.borrow(), scene.clear_layer_id);
        }

        composition.seal();

        context.render(self.index, needs_clear, &BACKGROUND_COLOR);

        // Keep reference to rasters for clearing.
        self.previous_rasters.push(hour_hand_raster);
        self.previous_rasters.push(hour_hand_shadow_raster);
        self.previous_rasters.push(minute_hand_raster);
        self.previous_rasters.push(minute_hand_shadow_raster);
        self.previous_rasters.push(second_hand_raster);
        self.previous_rasters.push(second_hand_shadow_raster);
    }
}

struct ClockfaceViewAssistant<T> {
    context: T,
    scene: Scene,
    contents: BTreeMap<u64, Contents>,
}

impl<T: Context> ClockfaceViewAssistant<T> {
    pub fn new(mut context: T) -> Self {
        let scene = Scene::new(&mut context);

        Self { context, scene, contents: BTreeMap::new() }
    }
}

impl<T: Context> ViewAssistant for ClockfaceViewAssistant<T> {
    fn setup(&mut self, _context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        let canvas = context.canvas.as_ref().unwrap().borrow();
        let size = &context.size;
        let context = &mut self.context;

        self.scene.update(context, size);

        // Temporary hack to deal with the fact that carnelian
        // allocates a new buffer for each frame with the same
        // image ID of zero.
        let mut temp_content;
        let content;

        if canvas.id == 0 {
            temp_content = Contents::new(canvas.index);
            content = &mut temp_content;
        } else {
            content = self.contents.entry(canvas.id).or_insert_with(|| Contents::new(canvas.index));
        }
        content.update(context, &self.scene, size);

        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::EveryFrame;
    }

    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat {
        self.context.get_pixel_format()
    }
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant::<ClockfaceAppAssistant>())
}
