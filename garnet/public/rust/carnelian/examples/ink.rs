// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    carnelian::{
        make_app_assistant, AnimationMode, App, AppAssistant, FrameBufferPtr, Point, Rect, Size,
        ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey, ViewMode,
    },
    euclid::{Angle, Transform2D, Vector2D},
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_hardware_input as hid, fidl_fuchsia_input_report as hid_input_report,
    fidl_fuchsia_sysmem::BufferCollectionTokenMarker,
    fuchsia_zircon::{self as zx, ClockId, Time},
    rand::{thread_rng, Rng},
    std::{collections::BTreeMap, env, f32, fs},
};

mod spinel_utils;

use crate::spinel_utils::{Context, MoldContext, Path, Raster, SpinelContext};

const APP_NAME: &'static [u8; 7] = b"ink_rs\0";
const BACKGROUND_COLOR: [f32; 4] = [1.0, 1.0, 1.0, 1.0];

// Stroke constants.
const STROKE_START_RADIUS: f32 = 0.25;
const STROKE_RADIUS_ADJUSTMENT_AMOUNT: f32 = 0.1;
const MAX_STROKES: usize = 1000;

// Toolbar constants.
const TOOL_RADIUS: f32 = 25.0;
const TOOL_PADDING: f32 = 12.5;

// Color palette constants.
const COLORS: [[f32; 4]; 6] = [
    [0.0, 0.0, 0.0, 1.0],
    [1.0, 1.0, 1.0, 1.0],
    [0.73, 0.29, 0.28, 0.8],
    [0.88, 0.82, 0.36, 0.8],
    [0.24, 0.52, 0.69, 0.8],
    [0.14, 0.50, 0.42, 0.8],
];

// Pencil constants.
const PENCILS: [f32; 3] = [1.5, 3.0, 10.0];

// Delay before starting to draw flowers after clearing the screen.
const FLOWER_DELAY_SECONDS: i64 = 10;

fn lerp(t: f32, p0: Point, p1: Point) -> Point {
    Point::new(p0.x * (1.0 - t) + p1.x * t, p0.y * (1.0 - t) + p1.y * t)
}

trait PathBuilder {
    fn line_to(&mut self, p: Point);
    fn cubic_to(&mut self, p0: Point, p1: Point, p2: Point, p3: Point, offset: Vector2D<f32>) {
        let deviation_x = (p0.x + p2.x - 3.0 * (p1.x + p2.x)).abs();
        let deviation_y = (p0.y + p2.y - 3.0 * (p1.y + p2.y)).abs();
        let deviation_squared = deviation_x * deviation_x + deviation_y * deviation_y;

        const PIXEL_ACCURACY: f32 = 0.25;

        if deviation_squared < PIXEL_ACCURACY {
            self.line_to(Point::new(p3.x, p3.y) + offset);
            return;
        }

        const TOLERANCE: f32 = 3.0;

        let subdivisions = 1 + (TOLERANCE * deviation_squared).sqrt().sqrt().floor() as usize;
        let increment = (subdivisions as f32).recip();

        let mut t = 0.0;

        for _ in 0..subdivisions - 1 {
            t += increment;
            let p_next = lerp(
                t,
                lerp(t, lerp(t, p0, p1), lerp(t, p1, p2)),
                lerp(t, lerp(t, p1, p2), lerp(t, p2, p3)),
            );

            self.line_to(Point::new(p_next.x, p_next.y) + offset);
        }
        self.line_to(Point::new(p3.x, p3.y) + offset);
    }
}

struct PointPathBuilder<'a> {
    points: &'a mut Vec<Point>,
}

impl<'a> PathBuilder for PointPathBuilder<'a> {
    fn line_to(&mut self, p: Point) {
        self.points.push(p);
    }
}

struct PathBuilderWrapper<'a> {
    path_builder: &'a mut dyn spinel_utils::PathBuilder,
}

impl<'a> PathBuilder for PathBuilderWrapper<'a> {
    fn line_to(&mut self, p: Point) {
        self.path_builder.line_to(&p);
    }
}

struct Circle {
    points: Vec<Point>,
}

impl Circle {
    fn new(center: Point, radius: f32) -> Self {
        let offset = center.to_vector();
        let dist = 4.0 / 3.0 * (f32::consts::PI / 8.0).tan();
        let control_dist = dist * radius;

        let t = Point::new(0.0, -radius);
        let r = Point::new(radius, 0.0);
        let b = Point::new(0.0, radius);
        let l = Point::new(-radius, 0.0);
        let ct = Point::new(0.0, -control_dist).to_vector();
        let cr = Point::new(control_dist, 0.0).to_vector();
        let cb = Point::new(0.0, control_dist).to_vector();
        let cl = Point::new(-control_dist, 0.0).to_vector();

        let mut points = Vec::new();
        points.push(t + offset);
        let mut path_builder = PointPathBuilder { points: &mut points };
        path_builder.cubic_to(t, t + cr, r + ct, r, offset);
        path_builder.cubic_to(r, r + cb, b + cr, b, offset);
        path_builder.cubic_to(b, b + cl, l + cb, l, offset);
        path_builder.cubic_to(l, l + ct, t + cl, t, offset);

        Self { points }
    }
}

struct Flower {
    points: Vec<Point>,
}

impl Flower {
    fn new(width: f32, height: f32) -> Self {
        const FLOWER_SIZE: f32 = 100.0;
        const FLOWER_MIN_PETALS: usize = 3;
        const FLOWER_MAX_PETALS: usize = 8;
        const FLOWER_MIN_R1: f32 = 60.0;
        const FLOWER_MAX_R1: f32 = 95.0;
        const FLOWER_MIN_R2: f32 = 20.0;
        const FLOWER_MAX_R2: f32 = 60.0;

        let mut rng = thread_rng();
        let petal_count: usize = rng.gen_range(FLOWER_MIN_PETALS, FLOWER_MAX_PETALS);
        let r1: f32 = rng.gen_range(FLOWER_MIN_R1, FLOWER_MAX_R1);
        let r2: f32 = rng.gen_range(FLOWER_MIN_R2, FLOWER_MAX_R2);
        // Random location in canvas.
        let offset = Vector2D::new(
            rng.gen_range(FLOWER_SIZE, width - FLOWER_SIZE),
            rng.gen_range(FLOWER_SIZE, height - FLOWER_SIZE),
        );

        let mut points = Vec::new();
        let u: f32 = rng.gen_range(10.0, FLOWER_SIZE) / FLOWER_SIZE;
        let v: f32 = rng.gen_range(0.0, FLOWER_SIZE - 10.0) / FLOWER_SIZE;
        let dt: f32 = f32::consts::PI / (petal_count as f32);
        let mut t: f32 = 0.0;

        let mut p0 = Point::new(t.cos() * r1, t.sin() * r1);
        points.push(p0 + offset);
        let mut path_builder = PointPathBuilder { points: &mut points };
        for _ in 0..petal_count {
            let x1 = t.cos() * r1;
            let y1 = t.sin() * r1;
            let x2 = (t + dt).cos() * r2;
            let y2 = (t + dt).sin() * r2;
            let x3 = (t + 2.0 * dt).cos() * r1;
            let y3 = (t + 2.0 * dt).sin() * r1;

            let p1 = Point::new(x1 - y1 * u, y1 + x1 * u);
            let p2 = Point::new(x2 + y2 * v, y2 - x2 * v);
            let p3 = Point::new(x2, y2);
            let p4 = Point::new(x2 - y2 * v, y2 + x2 * v);
            let p5 = Point::new(x3 + y3 * u, y3 - x3 * u);
            let p6 = Point::new(x3, y3);

            path_builder.cubic_to(p0, p1, p2, p3, offset);
            path_builder.cubic_to(p3, p4, p5, p6, offset);

            p0 = p6;
            t += dt * 2.0;
        }

        Self { points }
    }
}

#[derive(Default)]
struct InkAppAssistant;

impl AppAssistant for InkAppAssistant {
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
            Ok(Box::new(InkViewAssistant::new(MoldContext::new(token, config))))
        } else {
            const BLOCK_POOL_SIZE: u64 = 1 << 26; // 64 MB
            const HANDLE_COUNT: u32 = 1 << 14; // 16K handles
            const LAYERS_COUNT: u32 = (MAX_STROKES + (COLORS.len() + PENCILS.len()) * 2) as u32;
            const CMDS_COUNT: u32 = LAYERS_COUNT * 8 + 32;

            Ok(Box::new(InkViewAssistant::new(SpinelContext::new(
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

struct Fill {
    raster: Raster,
    color: [f32; 4],
}

impl Fill {
    fn new(context: &mut dyn Context, color: &[f32; 4], points: &Vec<Point>) -> Self {
        let path = {
            let path_builder = context.path_builder();
            path_builder.begin();
            let mut p0 = Point::zero();
            for (i, p) in points.iter().enumerate() {
                if i == 0 {
                    path_builder.move_to(&p);
                    p0 = *p;
                } else {
                    path_builder.line_to(&p);
                }
            }
            path_builder.line_to(&p0);
            path_builder.end()
        };

        let raster_builder = context.raster_builder();
        raster_builder.begin();
        const CLIP: [f32; 4] = [std::f32::MIN, std::f32::MIN, std::f32::MAX, std::f32::MAX];
        raster_builder.add(&path, &Transform2D::identity(), &CLIP);
        let raster = raster_builder.end();

        Self { raster, color: *color }
    }
}

struct Box2D {
    min: Point,
    max: Point,
}

impl Box2D {
    fn new() -> Self {
        Box2D {
            min: Point::new(std::f32::MAX, std::f32::MAX),
            max: Point::new(std::f32::MIN, std::f32::MIN),
        }
    }

    fn union(&self, point: &Point) -> Self {
        Box2D {
            min: Point::new(self.min.x.min(point.x), self.min.y.min(point.y)),
            max: Point::new(self.max.x.max(point.x), self.max.y.max(point.y)),
        }
    }

    fn to_rect(&self) -> Rect {
        Rect { origin: self.min, size: (self.max - self.min).to_size() }
    }
}

struct Segment {
    path: Path,
    bbox: Box2D,
    raster: Option<Raster>,
}

struct StrokePoint {
    point: Point,
    normal0: Vector2D<f32>,
    normal1: Vector2D<f32>,
    thickness: f32,
}

struct Stroke {
    points: Vec<StrokePoint>,
    segments: Vec<(usize, Segment)>,
    color: [f32; 4],
    thickness: f32,
    transform: Transform2D<f32>,
}

impl Stroke {
    fn new(color: [f32; 4], thickness: f32, transform: Transform2D<f32>) -> Self {
        Self { points: Vec::new(), segments: Vec::new(), color, thickness, transform }
    }

    fn raster(context: &mut dyn Context, path: &Path, transform: &Transform2D<f32>) -> Raster {
        let raster_builder = context.raster_builder();
        raster_builder.begin();
        const CLIP: [f32; 4] = [std::f32::MIN, std::f32::MIN, std::f32::MAX, std::f32::MAX];
        raster_builder.add(path, transform, &CLIP);

        raster_builder.end()
    }

    fn push_point(&mut self, p: &Point) {
        match self.points.len() {
            // Just add the first point.
            0 => self.points.push(StrokePoint {
                point: *p,
                normal0: Vector2D::zero(),
                normal1: Vector2D::zero(),
                thickness: STROKE_START_RADIUS,
            }),
            // Add second point and compute the normal for line between points.
            1 => {
                let p0 = self.points.pop().unwrap();
                let e = p0.point - *p;
                let n = Vector2D::new(-e.y, e.x).normalize();
                self.points.push(StrokePoint {
                    point: p0.point,
                    normal0: n,
                    normal1: n,
                    thickness: p0.thickness,
                });
                self.points.push(StrokePoint {
                    point: *p,
                    normal0: n,
                    normal1: n,
                    thickness: STROKE_START_RADIUS,
                });
            }
            // Add new point, compute the normal, and the average normal for last
            // two lines. We also make a limited adjustment to the average normal
            // distance to maintain the correct line thickness.
            _ => {
                let p1 = self.points.pop().unwrap();
                let p0 = self.points.pop().unwrap();
                let e = p1.point - *p;
                let n = Vector2D::new(-e.y, e.x).normalize();
                let mut t1 = (p1.normal1 + n) / 2.0;
                let l = t1.square_length().max(0.1);
                t1 *= 1.0 / l;
                self.points.push(StrokePoint {
                    point: p0.point,
                    normal0: p0.normal0,
                    normal1: p0.normal1,
                    thickness: p0.thickness,
                });
                self.points.push(StrokePoint {
                    point: p1.point,
                    normal0: p1.normal1,
                    normal1: t1,
                    thickness: p1.thickness,
                });
                self.points.push(StrokePoint {
                    point: *p,
                    normal0: n,
                    normal1: n,
                    thickness: STROKE_START_RADIUS,
                });
            }
        }
    }

    fn push_segment(&mut self, context: &mut dyn Context, i0: usize, i1: usize) {
        let (path, bbox) = {
            let mut bbox = Box2D::new();
            let path_builder = context.path_builder();
            path_builder.begin();

            //
            // Convert stroke to fill and compute a bounding box.
            //

            // Walk from point i0 to i1 and offset by radius at each point.
            let mut a0 = Point::zero();
            for (i, p) in self.points[i0..i1].iter().enumerate() {
                let a = p.point + p.normal1 * p.thickness;
                if i == 0 {
                    path_builder.move_to(&a);
                    a0 = a;
                } else {
                    path_builder.line_to(&a);
                }
                bbox = bbox.union(&a);
            }

            let p_first = &self.points.first().unwrap();
            let p_last = &self.points.last().unwrap();

            macro_rules! cap {
                ( $p:expr, $w:expr ) => {
                    let offset = $p.point.to_vector();
                    let n = Vector2D::new($p.normal0.y, -$p.normal0.x);
                    let p0 = Point::zero() + $p.normal1 * $w;
                    let p1 = Point::zero() - n * $w;
                    let p2 = Point::zero() - $p.normal1 * $w;

                    let dist = 4.0 / 3.0 * (f32::consts::PI / 8.0).tan();
                    let control_dist = dist * $w;

                    let c0 = p0 - n * control_dist;
                    let c1 = p1 + $p.normal1 * control_dist;
                    let c2 = p1 - $p.normal1 * control_dist;
                    let c3 = p2 - n * control_dist;

                    let mut wrapper = PathBuilderWrapper { path_builder };
                    wrapper.cubic_to(p0, c0, c1, p1, offset);
                    wrapper.cubic_to(p1, c2, c3, p2, offset);

                    bbox = bbox.union(&p0).union(&p1).union(&p2);
                };
            }

            // Produce end-cap if at the end of the line and not connected to first point.
            if i1 == self.points.len() && p_first.point != p_last.point {
                cap!(p_last, p_last.thickness);
            }

            // Walk from point i1 back to i0 and offset by radius at each point.
            for p in self.points[i0..i1].iter().rev() {
                let a = p.point - p.normal1 * p.thickness;
                path_builder.line_to(&a);
                bbox = bbox.union(&a);
            }

            // Produce start-cap if at the beginning of line and not connected to last point.
            if i0 == 0 && p_first.point != p_last.point {
                cap!(p_first, -p_first.thickness);
            }

            path_builder.line_to(&a0);

            (path_builder.end(), bbox)
        };

        self.segments.push((i0, Segment { path, bbox, raster: None }));
    }

    fn update_thickness(&mut self, context: &mut dyn Context) {
        assert_eq!(self.points.is_empty(), false);

        // No update needed if last point has correct thickness. This assumes
        // that last point always needs most adjustment.
        if self.points.last().unwrap().thickness == self.thickness {
            return;
        }

        let adjustment_amount = self.thickness * STROKE_RADIUS_ADJUSTMENT_AMOUNT;

        for p in self.points.iter_mut().rev() {
            if p.thickness == self.thickness {
                break;
            }
            p.thickness = if p.thickness > self.thickness {
                (p.thickness - adjustment_amount).max(self.thickness)
            } else {
                (p.thickness + adjustment_amount).min(self.thickness)
            };
        }

        // Remove and get index of first point in last segment.
        let mut i0 = self.segments.pop().map_or(0, |v| v.0);

        // Index of last point with final thickness.
        let i1 = self.points.iter().rposition(|v| v.thickness == self.thickness).unwrap_or(i0);

        const SEGMENT_SIZE: usize = 256;

        // Add segments with final thickness.
        while (i1 - i0) > SEGMENT_SIZE {
            let i = i0 + SEGMENT_SIZE;
            self.push_segment(context, i0, i);
            i0 = i - 1;
        }

        // Add any remaining points to last segment.
        if (self.points.len() - i0) > 1 {
            self.push_segment(context, i0, self.points.len());
        }
    }

    fn update(&mut self, context: &mut dyn Context, size: &Size) {
        // Update thickness expects at least one point.
        if self.points.is_empty() {
            return;
        }

        self.update_thickness(context);

        // Create raster if bounding box intersects size.
        let rect = Rect::from_size(*size);
        for (_, segment) in self.segments.iter_mut() {
            if segment.raster.is_none()
                && self.transform.transform_rect(&segment.bbox.to_rect()).intersects(&rect)
            {
                segment.raster = Some(Self::raster(context, &segment.path, &self.transform));
            }
        }
    }

    fn transform(&mut self, transform: &Transform2D<f32>) {
        self.transform = self.transform.post_mul(transform);

        // Re-create rasters during next call to update.
        for (_, segment) in self.segments.iter_mut() {
            segment.raster = None;
        }
    }
}

struct Scene {
    tools: Vec<(Stroke, Fill, Point)>,
    strokes: Vec<Stroke>,
}

impl Scene {
    fn new() -> Self {
        Self { tools: Vec::new(), strokes: Vec::new() }
    }

    fn update_styling(&mut self, context: &mut dyn Context) {
        let tools_count = self.tools.len() as u32;
        let stroke_count = self.strokes.len() as u32;
        let layers = tools_count * 2 + stroke_count;
        assert_ne!(layers, 0);
        let styling = context.styling();
        styling.unseal();
        styling.reset();
        let group_id = styling.alloc_group(0, layers - 1, &BACKGROUND_COLOR);
        for (i, (stroke, fill, _)) in self.tools.iter().enumerate() {
            styling.group_layer(&group_id, (i * 2) as u32, &stroke.color);
            styling.group_layer(&group_id, (i * 2 + 1) as u32, &fill.color);
        }
        let stroke_base = tools_count * 2;
        for (i, stroke) in self.strokes.iter_mut().rev().enumerate() {
            styling.group_layer(&group_id, stroke_base + i as u32, &stroke.color);
        }
        styling.seal();
    }

    fn setup(&mut self, context: &mut dyn Context, size: &Size, tools: &Vec<(&[f32; 4], &f32)>) {
        const TOOL_SIZE: f32 = (TOOL_RADIUS + TOOL_PADDING) * 2.0;

        // Layout tools at top-center.
        let mut x = size.width / 2.0 - (tools.len() as f32 * TOOL_SIZE) / 2.0;
        let y = TOOL_PADDING * 2.0 + TOOL_RADIUS;
        for (color, size) in tools {
            let center = Point::new(x, y);
            let circle = Circle::new(center, TOOL_RADIUS);
            let mut stroke = Stroke::new([0.0, 0.0, 0.0, 1.0], 1.0, Transform2D::identity());
            while stroke.points.len() < circle.points.len() {
                let p = &circle.points[stroke.points.len()];
                stroke.push_point(p);
            }
            let circle = Circle::new(center, **size);
            let fill = Fill::new(context, color, &circle.points);
            self.tools.push((stroke, fill, center));

            x += TOOL_SIZE;
        }

        self.update_styling(context);
    }

    fn hit_test(&mut self, point: Point) -> Option<usize> {
        for (i, (_, _, center)) in self.tools.iter().enumerate() {
            if (point - *center).length() < TOOL_RADIUS {
                return Some(i);
            }
        }

        None
    }

    fn select_tools(&mut self, indices: &Vec<usize>) {
        for (i, (stroke, _, _)) in self.tools.iter_mut().enumerate() {
            stroke.thickness = if indices.contains(&i) { 2.0 } else { 1.0 };
        }
    }

    fn push_stroke(&mut self, context: &mut dyn Context, color: [f32; 4], radius: f32) {
        self.strokes.push(Stroke::new(color, radius, Transform2D::identity()));
        self.update_styling(context);
    }

    fn last_stroke(&mut self) -> Option<&mut Stroke> {
        self.strokes.last_mut()
    }

    fn clear_strokes(&mut self) {
        self.strokes.clear();
    }

    fn update(&mut self, context: &mut dyn Context, size: &Size) {
        for (stroke, _, _) in self.tools.iter_mut() {
            stroke.update(context, size);
        }
        for stroke in self.strokes.iter_mut() {
            stroke.update(context, size);
        }
    }

    fn transform(&mut self, transform: &Transform2D<f32>) {
        for stroke in self.strokes.iter_mut() {
            stroke.transform(transform);
        }
    }
}

struct Contents {
    index: u32,
    size: Size,
}

impl Contents {
    fn new(index: u32) -> Self {
        Self { index, size: Size::zero() }
    }

    fn update(&mut self, context: &mut dyn Context, scene: &Scene, size: &Size) {
        let composition = context.composition(self.index);
        composition.unseal();
        composition.reset();

        if self.size != *size {
            self.size = *size;
            let clip: [u32; 4] = [0, 0, size.width.floor() as u32, size.height.floor() as u32];
            composition.set_clip(&clip);
        }

        for (i, (stroke, fill, _)) in scene.tools.iter().enumerate() {
            for segment in stroke.segments.iter() {
                if let Some(raster) = &segment.1.raster {
                    composition.place(raster, (i * 2) as u32);
                }
            }
            composition.place(&fill.raster, (i * 2 + 1) as u32);
        }

        let stroke_base = scene.tools.len() as u32 * 2;
        for (i, stroke) in scene.strokes.iter().rev().enumerate() {
            for segment in stroke.segments.iter() {
                if let Some(raster) = &segment.1.raster {
                    composition.place(raster, stroke_base + i as u32);
                }
            }
        }

        composition.seal();

        context.render(self.index, true, &BACKGROUND_COLOR);
    }
}

struct Stylus {
    _rpt_id: u8,
    status: u8,
    x: u16,
    y: u16,
}

// TODO: Remove stylus device when supported by carnelian.
struct StylusDevice {
    device: hid::DeviceSynchronousProxy,
    x_max: u16,
    y_max: u16,
}

impl StylusDevice {
    fn open_input_device(path: &str) -> Result<hid::DeviceSynchronousProxy, Error> {
        let (client, server) = zx::Channel::create()?;
        fdio::service_connect(path, server)?;
        Ok(hid::DeviceSynchronousProxy::new(client))
    }

    fn create() -> Result<StylusDevice, Error> {
        static INPUT_DEVICES_DIRECTORY: &str = "/dev/class/input";
        let path = std::path::Path::new(INPUT_DEVICES_DIRECTORY);
        let entries = fs::read_dir(path)?;
        for entry in entries {
            let entry = entry?;
            let entry_path = entry.path();
            let path = entry_path.to_str().expect("bad path");
            let mut device = Self::open_input_device(path)?;
            if let Ok(hid::DeviceIds { vendor_id: 0x00002d1f, product_id, .. }) =
                device.get_device_ids(zx::Time::INFINITE)
            {
                // Paradise
                if product_id == 0x00005143 {
                    println!("found stylus at {0}", path);
                    const PARADISE_STYLUS_X_MAX: u16 = 25919;
                    const PARADISE_STYLUS_Y_MAX: u16 = 17279;
                    return Ok(StylusDevice {
                        device,
                        x_max: PARADISE_STYLUS_X_MAX,
                        y_max: PARADISE_STYLUS_Y_MAX,
                    });
                }
                // Slate
                if product_id == 0x0000486c {
                    println!("found stylus at {0}", path);
                    const SLATE_STYLUS_X_MAX: u16 = 26009;
                    const SLATE_STYLUS_Y_MAX: u16 = 17339;
                    return Ok(StylusDevice {
                        device,
                        x_max: SLATE_STYLUS_X_MAX,
                        y_max: SLATE_STYLUS_Y_MAX,
                    });
                }
            }
        }
        Err(std::io::Error::new(std::io::ErrorKind::NotFound, "no touch found").into())
    }

    fn get_events(&mut self) -> Result<Vec<Stylus>, Error> {
        let mut stylus_events = Vec::<Stylus>::new();
        let reports = self.device.get_reports(zx::Time::INFINITE)?;
        let reports = reports.1;
        let mut report_index = 0;
        while report_index < reports.len() {
            let report = &reports[report_index..];
            if report[0] != 6 {
                report_index += 55;
                continue;
            }

            report_index += 20;
            stylus_events.push(Stylus {
                _rpt_id: report[0],
                status: report[1],
                x: report[2] as u16 + ((report[3] as u16) << 8),
                y: report[4] as u16 + ((report[5] as u16) << 8),
            });
        }
        Ok(stylus_events)
    }
}

// TODO: Remove touch device when supported by carnelian.
struct TouchDevice {
    device: hid_input_report::InputDeviceSynchronousProxy,
    x_range: hid_input_report::Range,
    y_range: hid_input_report::Range,
}

impl TouchDevice {
    fn create() -> Result<TouchDevice, Error> {
        let input_devices_directory = "/dev/class/input-report";
        let path = std::path::Path::new(input_devices_directory);
        let entries = fs::read_dir(path)?;
        for entry in entries {
            let entry = entry?;
            let (client, server) = zx::Channel::create()?;
            fdio::service_connect(entry.path().to_str().expect("bad path"), server)?;
            let mut device = hid_input_report::InputDeviceSynchronousProxy::new(client);

            let descriptor = device.get_descriptor(zx::Time::INFINITE)?;
            match descriptor.touch {
                None => continue,
                Some(touch) => {
                    println!("touch device: {0}", entry.path().to_str().unwrap());
                    let contact_descriptor = &touch.contacts.as_ref().unwrap()[0];
                    let x_range = contact_descriptor.position_x.as_ref().unwrap().range;
                    let y_range = contact_descriptor.position_y.as_ref().unwrap().range;
                    return Ok(TouchDevice { device, x_range, y_range });
                }
            }
        }
        Err(std::io::Error::new(std::io::ErrorKind::NotFound, "no touch device found").into())
    }

    fn get_events(&mut self) -> Result<Vec<hid_input_report::InputReport>, Error> {
        Ok(self.device.get_reports(zx::Time::INFINITE)?)
    }
}

struct InkViewAssistant<T> {
    context: T,
    scene: Scene,
    contents: BTreeMap<u64, Contents>,
    touch_device: Option<TouchDevice>,
    touch_points: Vec<Point>,
    stylus_device: Option<StylusDevice>,
    last_stylus_x: u16,
    last_stylus_y: u16,
    last_stylus_point: Option<Point>,
    flower: Option<Flower>,
    flower_start: Time,
    color: usize,
    pencil: usize,
    pan_origin: Vector2D<f32>,
    scale_distance: f32,
    rotation_angle: f32,
    clear_origin: Vector2D<f32>,
}

impl<T: Context> InkViewAssistant<T> {
    pub fn new(context: T) -> Self {
        let scene = Scene::new();
        let touch_device = TouchDevice::create().ok();
        let stylus_device = StylusDevice::create().ok();
        let flower_start = Time::from_nanos(
            Time::get(ClockId::Monotonic)
                .into_nanos()
                .saturating_add(zx::Duration::from_seconds(FLOWER_DELAY_SECONDS).into_nanos()),
        );

        Self {
            context,
            scene,
            contents: BTreeMap::new(),
            touch_device,
            touch_points: Vec::new(),
            stylus_device,
            last_stylus_x: std::u16::MAX,
            last_stylus_y: std::u16::MAX,
            last_stylus_point: None,
            flower: None,
            flower_start,
            color: 0,
            pencil: 1,
            pan_origin: Vector2D::zero(),
            scale_distance: 0.0,
            rotation_angle: 0.0,
            clear_origin: Vector2D::zero(),
        }
    }
}

impl<T: Context> ViewAssistant for InkViewAssistant<T> {
    fn setup(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        let size = &context.size;
        let context = &mut self.context;
        let color_iter = COLORS.iter().map(|color| (color, &TOOL_RADIUS));
        let pencil_iter = PENCILS.iter().map(|size| (&[0.0, 0.0, 0.0, 1.0], size));
        let tools = color_iter.chain(pencil_iter).collect::<Vec<_>>();
        self.scene.setup(context, size, &tools);
        self.scene.select_tools(&vec![self.color, COLORS.len() + self.pencil]);

        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        let time_now = Time::get(ClockId::Monotonic);
        let canvas = context.canvas.as_ref().unwrap().borrow();
        let size = &context.size;
        let context = &mut self.context;

        // Process touch device input.
        if let Some(device) = self.touch_device.as_mut() {
            let previous_touch_points_count = self.touch_points.len();

            let reports = device.get_events()?;
            for report in &reports {
                let touch = report.touch.as_ref();
                let contacts = touch.unwrap().contacts.as_ref().unwrap();
                self.touch_points.clear();
                for contact in contacts {
                    let point = Point::new(
                        size.width * contact.position_x.unwrap() as f32 / device.x_range.max as f32,
                        size.height * contact.position_y.unwrap() as f32
                            / device.y_range.max as f32,
                    );
                    self.touch_points.push(point);
                }
            }

            let mut transform = Transform2D::identity();

            // Pan and select color.
            match self.touch_points.len() {
                1 | 2 => {
                    let mut origin = Vector2D::zero();
                    for point in &self.touch_points {
                        origin += point.to_vector();
                    }
                    origin /= self.touch_points.len() as f32;
                    if self.touch_points.len() != previous_touch_points_count {
                        if let Some(index) = self.scene.hit_test(origin.to_point()) {
                            if index < COLORS.len() {
                                self.color = index;
                            } else {
                                self.pencil = index - COLORS.len();
                            }
                            self.scene.select_tools(&vec![self.color, COLORS.len() + self.pencil]);
                        }
                        self.pan_origin = origin;
                    }
                    let distance = origin - self.pan_origin;
                    transform = transform.post_translate(distance);
                    self.pan_origin = origin;
                }
                _ => {}
            }

            // Rotation & zoom.
            if self.touch_points.len() == 2 {
                let mut iter = self.touch_points.iter();
                let point0 = iter.next().unwrap();
                let point1 = iter.next().unwrap();

                let origin = (point0.to_vector() + point1.to_vector()) / 2.0;
                transform = transform.post_translate(-origin);

                // Rotation.
                let line = *point0 - *point1;
                let angle = line.x.atan2(line.y);
                if self.touch_points.len() != previous_touch_points_count {
                    self.rotation_angle = angle;
                }
                let rotation_angle = angle - self.rotation_angle;
                transform = transform.post_rotate(Angle::radians(rotation_angle));
                self.rotation_angle = angle;

                // Pinch to zoom.
                let distance = (*point0 - *point1).length();
                if distance != 0.0 {
                    if self.touch_points.len() != previous_touch_points_count {
                        self.scale_distance = distance;
                    }
                    let sxsy = distance / self.scale_distance;
                    transform = transform.post_scale(sxsy, sxsy);
                    self.scale_distance = distance;
                }

                transform = transform.post_translate(origin);
            }

            // Clear using 3 finger swipe across screen.
            if self.touch_points.len() >= 3 {
                let mut origin = Vector2D::zero();
                for point in &self.touch_points {
                    origin += point.to_vector();
                }
                origin /= self.touch_points.len() as f32;
                if self.touch_points.len() != previous_touch_points_count {
                    self.clear_origin = origin;
                }
                const MIN_CLEAR_SWIPE_DISTANCE: f32 = 512.0;
                let distance = (origin - self.clear_origin).length();
                if distance >= MIN_CLEAR_SWIPE_DISTANCE {
                    self.flower_start = Time::from_nanos(time_now.into_nanos().saturating_add(
                        zx::Duration::from_seconds(FLOWER_DELAY_SECONDS).into_nanos(),
                    ));
                    self.flower = None;
                    self.scene.clear_strokes();
                }
            }

            if transform != Transform2D::identity() {
                self.scene.transform(&transform);
            }
        }

        // Process stylus device input.
        if let Some(device) = self.stylus_device.as_mut() {
            let reports = device.get_events()?;
            for report in &reports {
                const STYLUS_STATUS_TSWITCH: u8 = 0x01;
                if (report.status & STYLUS_STATUS_TSWITCH) != 0 {
                    if report.x != self.last_stylus_x || report.y != self.last_stylus_y {
                        let point = Point::new(
                            size.width * report.x as f32 / device.x_max as f32,
                            size.height * report.y as f32 / device.y_max as f32,
                        );

                        // Start new stroke or select color.
                        if self.last_stylus_x == std::u16::MAX
                            || self.last_stylus_y == std::u16::MAX
                        {
                            if let Some(index) = self.scene.hit_test(point) {
                                if index < COLORS.len() {
                                    self.color = index;
                                } else {
                                    self.pencil = index - COLORS.len();
                                }
                                self.scene
                                    .select_tools(&vec![self.color, COLORS.len() + self.pencil]);
                            } else {
                                // Start stroke if we haven't reached the limit.
                                if self.scene.strokes.len() < MAX_STROKES {
                                    self.scene.push_stroke(
                                        context,
                                        COLORS[self.color],
                                        PENCILS[self.pencil],
                                    );
                                    self.scene.last_stroke().unwrap().push_point(&point);
                                    self.last_stylus_point = Some(point);
                                }
                                // Disable flower demo.
                                self.flower_start = zx::Time::INFINITE;
                                self.flower = None;
                            }
                        }

                        // Update stroke if distance from last point surpassed radius.
                        if let Some(last_stylus_point) = self.last_stylus_point {
                            if (point - last_stylus_point).length() > PENCILS[self.pencil] {
                                self.scene.last_stroke().unwrap().push_point(&point);
                                self.last_stylus_point = Some(point);
                            }
                        }

                        self.last_stylus_x = report.x;
                        self.last_stylus_y = report.y;
                    }
                } else {
                    self.last_stylus_x = std::u16::MAX;
                    self.last_stylus_y = std::u16::MAX;
                    self.last_stylus_point = None;
                }
            }
        }

        // Generate flower when idle after clearing screen.
        if time_now.into_nanos() > self.flower_start.into_nanos() {
            if self.flower.is_none() {
                self.scene.push_stroke(context, COLORS[self.color], PENCILS[self.pencil]);
            }

            let flower = self.flower.take().unwrap_or_else(|| Flower::new(size.width, size.height));

            // Points per second.
            const SPEED: f32 = 100.0;
            const SECONDS_PER_NANOSECOND: f32 = 1e-9;

            let n = ((time_now.into_nanos() - self.flower_start.into_nanos()) as f32
                * SECONDS_PER_NANOSECOND
                * SPEED) as usize;

            let stroke = self.scene.last_stroke().unwrap();

            // Extend set of points for current stroke.
            while n > stroke.points.len() && stroke.points.len() < flower.points.len() {
                let p = &flower.points[stroke.points.len()];
                stroke.push_point(p);
            }

            if stroke.points.len() == flower.points.len() {
                self.flower_start = if self.scene.strokes.len() < MAX_STROKES {
                    time_now
                } else {
                    zx::Time::INFINITE
                };
            } else {
                self.flower = Some(flower);
            }
        }

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
    App::run(make_app_assistant::<InkAppAssistant>())
}
