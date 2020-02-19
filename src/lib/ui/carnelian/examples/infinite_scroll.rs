// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    carnelian::{
        make_app_assistant, render::*, AnimationMode, App, AppAssistant, Color, FontFace,
        FrameBufferPtr, Point, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr,
        ViewKey, ViewMode,
    },
    euclid::{Point2D, Rect, Size2D, Transform2D, Vector2D},
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_input_report as hid_input_report,
    fidl_fuchsia_sysmem::BufferCollectionTokenMarker,
    fuchsia_async as fasync,
    fuchsia_trace::{self, counter, duration},
    fuchsia_trace_provider,
    fuchsia_zircon::{self as zx, ClockId, Time},
    futures::channel::mpsc::UnboundedReceiver,
    lazy_static::lazy_static,
    lipsum::{lipsum_title, lipsum_words},
    rand::{thread_rng, Rng},
    rusttype::{GlyphId, Scale, Segment},
    std::{
        cell::RefCell,
        collections::{BTreeMap, VecDeque},
        f32, fs,
        rc::Rc,
    },
    textwrap::wrap_iter,
};

const BACKGROUND_COLOR: Color = Color { r: 255, g: 255, b: 255, a: 255 };

// Clamp scroll offset to this range for sanity.
const SCROLL_OFFSET_RANGE: [u32; 2] = [0, 1000000];

// Delay before starting to simulate scrolling.
const FAKE_SCROLL_DELAY_SECONDS: i64 = 10;

// Available scrolling methods.
#[derive(Copy, Clone, Debug)]
enum ScrollMethod {
    Redraw,
    CopyRedraw,
    SlidingOffset,
    MotionBlur,
}

impl std::str::FromStr for ScrollMethod {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "Redraw" => Ok(Self::Redraw),
            "CopyRedraw" => Ok(Self::CopyRedraw),
            "SlidingOffset" => Ok(Self::SlidingOffset),
            "MotionBlur" => Ok(Self::MotionBlur),
            _ => Err(format!("'{}' is not a valid value for ScrollMethod", s)),
        }
    }
}

// This font creation method isn't ideal. The correct method would be to ask the Fuchsia
// font service for the font data.
static FONT_DATA: &'static [u8] =
    include_bytes!("../../../../../prebuilt/third_party/fonts/robotoslab/RobotoSlab-Regular.ttf");

lazy_static! {
    pub static ref FONT_FACE: FontFace<'static> =
        FontFace::new(&FONT_DATA).expect("Failed to create font");
}

/// Infinite scroll.
#[derive(Debug, FromArgs)]
#[argh(name = "infinite_scroll_rs")]
struct Args {
    /// use mold (software rendering back-end)
    #[argh(switch, short = 'm')]
    use_mold: bool,

    /// contents scale (default scale is 2.0)
    #[argh(option, default = "2.0")]
    scale: f32,

    /// scrolling method (Redraw|CopyRedraw|SlidingOffset|MotionBlur)
    #[argh(option, default = "ScrollMethod::MotionBlur")]
    scroll_method: ScrollMethod,

    /// motion blur exposure time
    #[argh(option, default = "1.0")]
    exposure: f32,

    /// touch sampling offset (relative to presentation time)
    #[argh(option, default = "30")]
    touch_sampling_offset_ms: i64,

    /// maximum number of columns
    #[argh(option, default = "std::u32::MAX")]
    max_columns: u32,

    /// disable text (improves rendering performance)
    #[argh(switch)]
    disable_text: bool,
}

#[derive(Default)]
struct InfiniteScrollAppAssistant;

impl AppAssistant for InfiniteScrollAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant_image_pipe(
        &mut self,
        _: ViewKey,
        fb: FrameBufferPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        let args: Args = argh::from_env();
        println!("back-end: {}", if args.use_mold { "mold" } else { "spinel" });
        println!("scale: {}", args.scale);
        println!("scroll method: {:?}", args.scroll_method);
        match args.scroll_method {
            ScrollMethod::MotionBlur => {
                println!("exposure: {}", args.exposure);
            }
            _ => {}
        }
        println!("touch sampling offset: {} ms", args.touch_sampling_offset_ms);

        let (token, token_request) =
            create_endpoints::<BufferCollectionTokenMarker>().expect("create_endpoint");
        fb.borrow()
            .local_token
            .as_ref()
            .unwrap()
            .duplicate(std::u32::MAX, token_request)
            .expect("duplicate");
        let config = &fb.borrow().get_config();
        let size = Size2D::new(config.width, config.height);

        if args.use_mold {
            Ok(Box::new(InfiniteScrollViewAssistant::new(
                Mold::new_context(token, size),
                args.scale,
                args.scroll_method,
                args.exposure,
                zx::Duration::from_millis(args.touch_sampling_offset_ms),
                args.max_columns,
                args.disable_text,
            )))
        } else {
            Ok(Box::new(InfiniteScrollViewAssistant::new(
                Spinel::new_context(token, size),
                args.scale,
                args.scroll_method,
                args.exposure,
                zx::Duration::from_millis(args.touch_sampling_offset_ms),
                args.max_columns,
                args.disable_text,
            )))
        }
    }

    fn get_mode(&self) -> ViewMode {
        ViewMode::ImagePipe
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

    fn to_rect(&self) -> Rect<f32> {
        Rect { origin: self.min, size: (self.max - self.min).to_size() }
    }
}

fn lerp(t: f32, p0: Point, p1: Point) -> Point {
    Point::new(p0.x * (1.0 - t) + p1.x * t, p0.y * (1.0 - t) + p1.y * t)
}

// TODO: Remove and use spn_path_builder_cubic_to.
fn cubic<B: Backend>(
    path_builder: &mut impl PathBuilder<B>,
    p0: Point,
    p1: Point,
    p2: Point,
    p3: Point,
    bounding_box: &mut Box2D,
) {
    duration!("gfx", "cubic");

    path_builder.move_to(p0);
    path_builder.cubic_to(p1, p2, p3);

    let deviation_x = (p0.x + p2.x - 3.0 * (p1.x + p2.x)).abs();
    let deviation_y = (p0.y + p2.y - 3.0 * (p1.y + p2.y)).abs();
    let deviation_squared = deviation_x * deviation_x + deviation_y * deviation_y;

    const PIXEL_ACCURACY: f32 = 0.25;
    if deviation_squared < PIXEL_ACCURACY {
        *bounding_box = bounding_box.union(&p0).union(&p3);
        return;
    }

    const TOLERANCE: f32 = 3.0;
    let subdivisions = 1 + (TOLERANCE * deviation_squared).sqrt().sqrt().floor() as usize;
    let increment = (subdivisions as f32).recip();
    let mut t = 0.0;

    *bounding_box = bounding_box.union(&p0);
    for _ in 0..subdivisions - 1 {
        t += increment;
        let p_next = lerp(
            t,
            lerp(t, lerp(t, p0, p1), lerp(t, p1, p2)),
            lerp(t, lerp(t, p1, p2), lerp(t, p2, p3)),
        );
        *bounding_box = bounding_box.union(&p_next);
    }
    *bounding_box = bounding_box.union(&p3);
}

struct Glyph<B: Backend> {
    path: Rc<RefCell<B::Path>>,
    bounding_box: Rect<f32>,
}

impl<B: Backend> Glyph<B> {
    fn new(context: &mut impl Context<B>, face: &FontFace<'_>, size: f32, id: GlyphId) -> Self {
        duration!("gfx", "Glyph::new");

        let mut path_builder = context.path_builder().unwrap();
        let mut bounding_box = Box2D::new();
        let scale = Scale::uniform(size);

        macro_rules! flip_y {
            ( $p:expr ) => {
                Point::new($p.x, -$p.y)
            };
        }

        let glyph = face.font.glyph(id).scaled(scale);
        if let Some(glyph_box) = glyph.exact_bounding_box() {
            let contours = glyph.shape().unwrap();
            for contour in contours {
                for segment in &contour.segments {
                    match segment {
                        Segment::Line(line) => {
                            path_builder.move_to(flip_y!(line.p[1]));
                            path_builder.line_to(flip_y!(line.p[0]));
                        }
                        Segment::Curve(curve) => {
                            path_builder.move_to(flip_y!(curve.p[2]));
                            path_builder.quad_to(flip_y!(curve.p[1]), flip_y!(curve.p[0]));
                        }
                    }
                }
            }

            bounding_box = bounding_box.union(&Point::new(glyph_box.min.x, glyph_box.min.y));
            bounding_box = bounding_box.union(&Point::new(glyph_box.max.x, glyph_box.max.y));
        }

        Self {
            path: Rc::new(RefCell::new(path_builder.build())),
            bounding_box: bounding_box.to_rect(),
        }
    }
}

struct Text<B: Backend> {
    paths: Vec<(Vector2D<f32>, Rc<RefCell<B::Path>>, Rect<f32>, Option<B::Raster>)>,
    bounding_box: Rect<f32>,
}

impl<B: Backend> Text<B> {
    fn new(
        context: &mut impl Context<B>,
        text: &str,
        size: f32,
        wrap: usize,
        face: &FontFace<'_>,
        glyphs: &mut BTreeMap<GlyphId, Glyph<B>>,
    ) -> Self {
        duration!("gfx", "Text::new");

        let mut bounding_box = Rect::zero();
        let scale = Scale::uniform(size);
        let v_metrics = face.font.v_metrics(scale);
        let mut ascent = v_metrics.ascent;
        let mut paths = Vec::new();

        for line in wrap_iter(text, wrap) {
            let y_offset = Vector2D::new(0.0, ascent);
            let chars = line.chars();
            let mut x = 0.0;
            let mut last = None;
            for g in face.font.glyphs_for(chars) {
                let g = g.scaled(scale);
                let id = g.id();
                let w = g.h_metrics().advance_width
                    + last.map(|last| face.font.pair_kerning(scale, last, id)).unwrap_or(0.0);
                let position = y_offset + Vector2D::new(x, 0.0);
                let glyph = glyphs.entry(id).or_insert_with(|| Glyph::new(context, face, size, id));
                paths.push((position, glyph.path.clone(), glyph.bounding_box, None));
                let glyph_bounding_box = glyph.bounding_box.translate(&position);
                if bounding_box.is_empty() {
                    bounding_box = glyph_bounding_box;
                } else {
                    bounding_box = bounding_box.union(&glyph_bounding_box);
                }
                x += w;
                last = Some(id);
            }
            ascent += size;
        }

        Self { paths, bounding_box }
    }
}

struct Flower<B: Backend> {
    path: B::Path,
    bounding_box: Rect<f32>,
}

impl<B: Backend> Flower<B> {
    fn new(context: &mut impl Context<B>, petal_count: usize, r1: f32, r2: f32) -> Self {
        duration!("gfx", "Flower::new");

        let mut bounding_box = Box2D::new();
        let mut path_builder = context.path_builder().unwrap();
        let mut rng = thread_rng();
        let u: f32 = rng.gen_range(10.0, 100.0) / 100.0;
        let v: f32 = rng.gen_range(0.0, 90.0) / 100.0;
        let dt: f32 = f32::consts::PI / (petal_count as f32);
        let mut t: f32 = 0.0;

        let mut p0 = Point::new(t.cos() * r1, t.sin() * r1);
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

            cubic(&mut path_builder, p0, p1, p2, p3, &mut bounding_box);
            cubic(&mut path_builder, p3, p4, p5, p6, &mut bounding_box);

            p0 = p6;
            t += dt * 2.0;
        }

        Self { path: path_builder.build(), bounding_box: bounding_box.to_rect() }
    }
}

fn random_color_element() -> u8 {
    let mut rng = thread_rng();
    let c: u8 = rng.gen_range(0, 255);
    c
}

fn random_color() -> Color {
    Color {
        r: random_color_element(),
        g: random_color_element(),
        b: random_color_element(),
        a: 255,
    }
}

const ITEM_TYPE_TITLE: i32 = 0;
const ITEM_TYPE_BODY_MIN: i32 = 1;
const ITEM_TYPE_BODY_MAX: i32 = 3;
const ITEM_TYPE_FLOWER: i32 = 4;
const ITEM_TYPE_COUNT: i32 = 5;

struct Item<B: Backend> {
    paths: Vec<(Vector2D<f32>, Rc<RefCell<B::Path>>, Rect<f32>, Option<B::Raster>)>,
    origin: Point,
    bounding_box: (Rect<f32>, B::Path, Option<B::Raster>),
    txty: [f32; 2],
    color: Color,
    id: i32,
}

struct Scene<B: Backend> {
    scale: f32,
    exposure: f32,
    max_columns: u32,
    text_disabled: bool,
    scroll_offset_y: u32,
    last_scroll_offset_y: u32,
    raster_ty: f32,
    title_glyphs: BTreeMap<GlyphId, Glyph<B>>,
    body_glyphs: BTreeMap<GlyphId, Glyph<B>>,
    columns: Vec<VecDeque<Item<B>>>,
    total_items: usize,
}

impl<B: Backend> Scene<B> {
    fn new(scale: f32, exposure: f32, max_columns: u32, text_disabled: bool) -> Self {
        // Equal amount of coordinate space above and below.
        let scroll_offset_y = (SCROLL_OFFSET_RANGE[0] + SCROLL_OFFSET_RANGE[1]) / 2;

        Self {
            scale,
            exposure,
            max_columns,
            text_disabled,
            scroll_offset_y: scroll_offset_y,
            last_scroll_offset_y: scroll_offset_y,
            raster_ty: -(scroll_offset_y as f32),
            title_glyphs: BTreeMap::new(),
            body_glyphs: BTreeMap::new(),
            columns: Vec::new(),
            total_items: 0,
        }
    }

    fn new_item(
        context: &mut impl Context<B>,
        scale: f32,
        title_glyphs: &mut BTreeMap<GlyphId, Glyph<B>>,
        body_glyphs: &mut BTreeMap<GlyphId, Glyph<B>>,
        id: i32,
        column_width: f32,
        text_disabled: bool,
    ) -> Item<B> {
        let item_type =
            if text_disabled { ITEM_TYPE_FLOWER } else { id.rem_euclid(ITEM_TYPE_COUNT) };
        // Alternate between title, body, and flower shape.
        let (paths, bounding_box, origin, color) = match item_type {
            ITEM_TYPE_TITLE => {
                const TITLE_SIZE: f32 = 32.0;

                let size = TITLE_SIZE * scale;
                let wrap = 2.0 * column_width / size as f32;
                let title = lipsum_title();
                let text =
                    Text::new(context, &title, size, wrap as usize, &FONT_FACE, title_glyphs);

                (
                    text.paths,
                    text.bounding_box.round_out(),
                    Point::new(0.0, 0.0),
                    Color { r: 0, g: 0, b: 0, a: 255 },
                )
            }
            ITEM_TYPE_BODY_MIN..=ITEM_TYPE_BODY_MAX => {
                const BODY_SIZE: f32 = 20.0;
                const BODY_MIN_WORDS: usize = 25;
                const BODY_MAX_WORDS: usize = 100;

                let size = BODY_SIZE * scale;
                let wrap = 2.75 * column_width / size as f32;
                let mut rng = thread_rng();
                let body = lipsum_words(rng.gen_range(BODY_MIN_WORDS, BODY_MAX_WORDS));
                let text = Text::new(context, &body, size, wrap as usize, &FONT_FACE, body_glyphs);

                (
                    text.paths,
                    text.bounding_box.round_out(),
                    Point::new(0.0, 0.0),
                    Color { r: 0, g: 0, b: 0, a: 255 },
                )
            }
            ITEM_TYPE_FLOWER => {
                const FLOWER_MIN_PETALS: usize = 3;
                const FLOWER_MAX_PETALS: usize = 8;
                const FLOWER_MIN_R1: f32 = 60.0;
                const FLOWER_MAX_R1: f32 = 95.0;
                const FLOWER_MIN_R2: f32 = 20.0;
                const FLOWER_MAX_R2: f32 = 60.0;

                let mut rng = thread_rng();
                let petal_count: usize = rng.gen_range(FLOWER_MIN_PETALS, FLOWER_MAX_PETALS);
                let r1: f32 = rng.gen_range(FLOWER_MIN_R1, FLOWER_MAX_R1) * scale;
                let r2: f32 = rng.gen_range(FLOWER_MIN_R2, FLOWER_MAX_R2) * scale;
                let flower = Flower::new(context, petal_count, r1, r2);
                let x = column_width / 2.0;
                let bounding_box = flower.bounding_box.round_out();

                (
                    vec![(
                        Vector2D::zero(),
                        Rc::new(RefCell::new(flower.path)),
                        bounding_box,
                        None,
                    )],
                    bounding_box,
                    Point::new(x, 0.0),
                    random_color(),
                )
            }
            _ => unreachable!(),
        };

        let mut path_builder = context.path_builder().unwrap();
        path_builder.move_to(bounding_box.origin);
        path_builder.line_to(bounding_box.top_right());
        path_builder.line_to(bounding_box.bottom_right());
        path_builder.line_to(bounding_box.bottom_left());
        path_builder.line_to(bounding_box.origin);

        let bounding_box = bounding_box.translate(&origin.to_vector());

        Item {
            paths,
            origin,
            bounding_box: (bounding_box, path_builder.build(), None),
            txty: [0.0, 0.0],
            color,
            id,
        }
    }

    fn update(&mut self, context: &mut impl Context<B>, size: &Size, scroll_delta: i32) {
        counter!("gfx", "scroll_dy", 0, "dy" => scroll_delta.abs());

        let new_offset = self.scroll_offset_y as i32 + scroll_delta;
        self.last_scroll_offset_y = self.scroll_offset_y;
        self.scroll_offset_y =
            new_offset.max(SCROLL_OFFSET_RANGE[0] as i32).min(SCROLL_OFFSET_RANGE[1] as i32) as u32;
        let top = self.scroll_offset_y as f32;

        const COLUMN_SIZE: f32 = 400.0;
        const PADDING: f32 = 50.0;
        const MIN_MARGIN: f32 = 50.0;
        const DISCARD_EXTRA_DISTANCE: f32 = 2048.0;

        let column_size = (COLUMN_SIZE * self.scale) as usize;
        let padding = PADDING * self.scale;
        let min_margin = (MIN_MARGIN * self.scale) as usize;
        let width = size.width as usize;
        let column_count = ((width - min_margin * 2) / (column_size + min_margin))
            .min(self.max_columns as usize)
            .max(1);
        let margin = (width - column_count * column_size) / (column_count + 1);

        if column_count != self.columns.len() {
            self.columns = (0..column_count).map(|_| VecDeque::new()).collect();
        }

        // Skip random number of body items.
        fn get_id_step(id: i32, base: i32) -> i32 {
            if id.rem_euclid(ITEM_TYPE_COUNT) == base {
                let mut rng = thread_rng();
                rng.gen_range(1, (ITEM_TYPE_BODY_MAX - ITEM_TYPE_BODY_MIN) + 1)
            } else {
                1
            }
        }

        // Discard distance needs to be at least height for clearing. Extra distance
        // avoids unncessary work by keeping items around that are likely to become
        // visible again. This improves performance at the cost of increased memory
        // usage.
        let discard_distance = size.height + DISCARD_EXTRA_DISTANCE;

        self.total_items = 0;
        for (i, column) in self.columns.iter_mut().enumerate() {
            let margin = margin + (column_size + margin) * i;

            // Remove items at the bottom of column that are no longer visible and
            // outside discard distance.
            while !column.is_empty()
                && (column.back().unwrap().bounding_box.0.min_y() - top)
                    > (size.height + discard_distance)
            {
                column.pop_back();
            }

            // Remove items at the top of column that are no longer visible and
            // outside discard distance.
            while !column.is_empty()
                && (column.front().unwrap().bounding_box.0.max_y() - top) < -discard_distance
            {
                column.pop_front();
            }

            // Add new items at the bottom of column to fill visible region.
            while column.is_empty()
                || (column.back().unwrap().bounding_box.0.max_y() - top + padding) < size.height
            {
                let (id, y) = if let Some(item) = column.back() {
                    (
                        item.id + get_id_step(item.id, ITEM_TYPE_BODY_MIN),
                        item.bounding_box.0.max_y(),
                    )
                } else {
                    (0, top)
                };
                let mut item = Self::new_item(
                    context,
                    self.scale,
                    &mut self.title_glyphs,
                    &mut self.body_glyphs,
                    id,
                    column_size as f32,
                    self.text_disabled,
                );
                let offset =
                    Vector2D::new(margin as f32, y - item.bounding_box.0.min_y() + padding);
                item.origin += offset;
                item.bounding_box.0 = item.bounding_box.0.translate(&offset);
                item.txty[1] = self.raster_ty;
                column.push_back(item);
            }

            // Add new items at the top of column to fill visible region.
            while column.is_empty()
                || (column.front().unwrap().bounding_box.0.min_y() - top - padding) > 0.0
            {
                let (id, y) = if let Some(item) = column.front() {
                    (
                        item.id - get_id_step(item.id, ITEM_TYPE_BODY_MAX),
                        item.bounding_box.0.min_y(),
                    )
                } else {
                    (0, top + size.height as f32)
                };
                let mut item = Self::new_item(
                    context,
                    self.scale,
                    &mut self.title_glyphs,
                    &mut self.body_glyphs,
                    id,
                    column_size as f32,
                    self.text_disabled,
                );
                let offset = Vector2D::new(
                    margin as f32,
                    y - item.bounding_box.0.size.height - item.bounding_box.0.min_y() - padding,
                );
                item.origin += offset;
                item.bounding_box.0 = item.bounding_box.0.translate(&offset);
                item.txty[1] = self.raster_ty;
                column.push_front(item);
            }

            self.total_items += column.len();
        }
    }

    fn set_raster_ty(&mut self, raster_ty: f32) {
        if raster_ty != self.raster_ty {
            let dy = raster_ty - self.raster_ty;
            for item in self.columns.iter_mut().flat_map(|c| c.iter_mut()) {
                item.txty[1] += dy;
                item.bounding_box.2 = None;
                for (_, _, _, raster) in item.paths.iter_mut() {
                    raster.take();
                }
            }
            self.raster_ty = raster_ty;
        }
    }

    fn set_items_ty(&mut self, ty: f32) {
        for item in self.columns.iter_mut().flat_map(|c| c.iter_mut()) {
            item.txty[1] = ty;
            item.bounding_box.2 = None;
            for (_, _, _, raster) in item.paths.iter_mut() {
                raster.take();
            }
        }
    }

    fn build_rasters_for_viewport(&mut self, context: &mut impl Context<B>, viewport: &Rect<f32>) {
        for item in self.columns.iter_mut().flat_map(|c| c.iter_mut()) {
            if viewport.intersects(&item.bounding_box.0) {
                for (offset, path, bounding_box, raster) in item.paths.iter_mut() {
                    let bounding_box = bounding_box.translate(&Vector2D::new(
                        item.origin.x + offset.x,
                        item.origin.y + offset.y,
                    ));
                    if raster.is_none() && viewport.intersects(&bounding_box) {
                        let new_raster = {
                            let mut raster_builder = context.raster_builder().unwrap();
                            let transform = Transform2D::create_translation(
                                item.origin.x + offset.x + item.txty[0],
                                item.origin.y + offset.y + item.txty[1],
                            );
                            raster_builder.add(&path.borrow(), Some(&transform));
                            raster_builder.build()
                        };
                        raster.replace(new_raster);
                    }
                }
            }
        }
    }

    fn build_clear_rasters_for_viewport(
        &mut self,
        context: &mut impl Context<B>,
        viewport: &Rect<f32>,
    ) {
        for item in self.columns.iter_mut().flat_map(|c| c.iter_mut()) {
            if item.bounding_box.2.is_none() && viewport.intersects(&item.bounding_box.0) {
                let new_raster = {
                    let mut raster_builder = context.raster_builder().unwrap();
                    let transform = Transform2D::create_translation(
                        item.origin.x + item.txty[0],
                        item.origin.y + item.txty[1],
                    );
                    raster_builder.add(&item.bounding_box.1, Some(&transform));
                    raster_builder.build()
                };
                item.bounding_box.2.replace(new_raster);
            }
        }
    }

    fn prepend_clear_layers_to_composition(
        &self,
        composition: &mut B::Composition,
        viewport: &Rect<f32>,
    ) {
        let layers = self
            .columns
            .iter()
            .flat_map(|column| column.iter())
            .filter(|item| viewport.intersects(&item.bounding_box.0))
            .filter_map(|item| {
                if let Some(raster) = &item.bounding_box.2 {
                    Some(Layer {
                        raster: raster.clone(),
                        style: Style {
                            fill_rule: FillRule::WholeTile,
                            fill: Fill::Solid(BACKGROUND_COLOR),
                            blend_mode: BlendMode::Over,
                        },
                    })
                } else {
                    None
                }
            });

        composition.splice(..0, layers);
    }

    fn prepend_layers_to_composition(
        &self,
        composition: &mut B::Composition,
        viewport: &Rect<f32>,
    ) {
        let layers = self
            .columns
            .iter()
            .flat_map(|column| column.iter())
            .filter(|item| viewport.intersects(&item.bounding_box.0))
            .filter_map(|item| {
                if let Some(raster) = item.paths.iter().fold(
                    None,
                    |raster_union: Option<B::Raster>, (_, _, _, raster)| {
                        if let Some(raster) = raster {
                            if let Some(raster_union) = raster_union {
                                Some(raster_union + raster.clone())
                            } else {
                                Some(raster.clone())
                            }
                        } else {
                            raster_union
                        }
                    },
                ) {
                    Some(Layer {
                        raster,
                        style: Style {
                            fill_rule: FillRule::NonZero,
                            fill: Fill::Solid(item.color),
                            blend_mode: BlendMode::Over,
                        },
                    })
                } else {
                    None
                }
            });

        composition.splice(..0, layers);
    }
}

struct Contents<B: Backend> {
    image: B::Image,
    scroll_method: ScrollMethod,
    scroll_offset_y: u32,
    size: Size,
    composition: B::Composition,
}

impl<B: Backend> Contents<B> {
    fn new(image: B::Image, scroll_method: ScrollMethod) -> Self {
        let composition = Composition::new(std::iter::empty(), BACKGROUND_COLOR);

        Self { image, scroll_method, scroll_offset_y: 0, size: Size::zero(), composition }
    }

    fn update(
        &mut self,
        context: &mut impl Context<B>,
        scene: &mut Scene<B>,
        size: &Size,
        staging_image: &mut Option<B::Image>,
    ) {
        let width = size.width.floor() as u32;
        let height = size.height.floor() as u32;

        let mut ext = if self.size != *size {
            RenderExt {
                pre_clear: Some(PreClear { color: BACKGROUND_COLOR }),
                ..Default::default()
            }
        } else {
            RenderExt::default()
        };

        match self.scroll_method {
            // Method 1: Translate paths and redraw the whole scene each frame.
            ScrollMethod::Redraw => {
                // Add clear layers from previous viewport.
                let viewport = Rect::new(
                    Point::new(0.0, self.scroll_offset_y as f32),
                    Size::new(width as f32, height as f32),
                );
                scene.set_raster_ty(-(self.scroll_offset_y as f32));
                scene.build_clear_rasters_for_viewport(context, &viewport);
                self.composition.splice(.., std::iter::empty::<Layer<B>>());
                scene.prepend_clear_layers_to_composition(&mut self.composition, &viewport);

                // Add layers for current viewport.
                let viewport = Rect::new(
                    Point::new(0.0, scene.scroll_offset_y as f32),
                    Size::new(width as f32, height as f32),
                );
                scene.set_raster_ty(-(scene.scroll_offset_y as f32));
                scene.build_rasters_for_viewport(context, &viewport);
                scene.prepend_layers_to_composition(&mut self.composition, &viewport);

                // Render scene.
                context.render(&self.composition, None, self.image, &ext);
            }
            // Method 2: Copy the image area that is still visible and redraw exposed region.
            ScrollMethod::CopyRedraw => {
                let scroll_distance = scene.scroll_offset_y as f32 - self.scroll_offset_y as f32;
                let scroll_amount = scroll_distance.abs() as u32;
                // |scroll_height| is the area that can be copied and doesn't need to a redraw.
                let scroll_height = if self.size == *size {
                    // Round down to multiple of 32. This ensures that we can use the render clip
                    // to redraw the exposed region.
                    (height - scroll_amount.min(height)) & !31
                } else {
                    0
                };
                let mut clip = Rect::new(Point2D::new(0, 0), Size2D::new(width, height));
                // Determine area to copy and clip to render depending on scroll direction.
                if scroll_height > 0 {
                    match scroll_distance {
                        // Area at top exposed.
                        x if x > 0.0 => {
                            clip.origin.y = scroll_height;
                            clip.size.height = height - scroll_height;
                            ext.pre_copy = Some(PreCopy {
                                image: self.image,
                                copy_region: CopyRegion {
                                    src_offset: Point2D::new(0, scroll_amount),
                                    dst_offset: Point2D::new(0, 0),
                                    extent: Size2D::new(width, height - scroll_amount),
                                },
                            });
                        }
                        // Area at bottom exposed.
                        x if x < 0.0 => {
                            clip.origin.y = 0;
                            clip.size.height = height - scroll_height;
                            ext.pre_copy = Some(PreCopy {
                                image: self.image,
                                copy_region: CopyRegion {
                                    src_offset: Point2D::new(0, 0),
                                    dst_offset: Point2D::new(0, scroll_amount),
                                    extent: Size2D::new(width, height - scroll_amount),
                                },
                            });
                        }
                        // No new contents exposed.
                        _ => {
                            clip.size.height = 0;
                        }
                    }
                }

                // Add clear layers from previous viewport of clip.
                let viewport = Rect::new(
                    Point::new(
                        clip.origin.x as f32,
                        clip.origin.y as f32 + self.scroll_offset_y as f32,
                    ),
                    Size::new(clip.size.width as f32, clip.size.height as f32),
                );
                scene.set_raster_ty(-(self.scroll_offset_y as f32));
                scene.build_clear_rasters_for_viewport(context, &viewport);
                self.composition.splice(.., std::iter::empty::<Layer<B>>());
                scene.prepend_clear_layers_to_composition(&mut self.composition, &viewport);

                // Add layers for current viewport of clip.
                let viewport = Rect::new(
                    Point::new(
                        clip.origin.x as f32,
                        clip.origin.y as f32 + scene.scroll_offset_y as f32,
                    ),
                    Size::new(clip.size.width as f32, clip.size.height as f32),
                );
                scene.set_raster_ty(-(scene.scroll_offset_y as f32));
                scene.build_rasters_for_viewport(context, &viewport);
                scene.prepend_layers_to_composition(&mut self.composition, &viewport);

                // Render clip.
                context.render(&self.composition, Some(clip), self.image, &ext);
            }
            // Method 3: Allocate temporary buffer and use a sliding offset to minimize
            // redraw. Contents of temporary buffer is copied to image after rendering.
            // Method 4: Sliding offset method with motion blur effect.
            ScrollMethod::SlidingOffset | ScrollMethod::MotionBlur => {
                // Add 32 and round temporary buffer height up to multiple of 32 in order
                // to be able to use the render clip.
                let buffer_height = (height + 63) & !31;
                let top = scene.scroll_offset_y;
                let bottom = scene.scroll_offset_y + height;
                let next_y0 = top % buffer_height;
                let next_y1 = bottom % buffer_height;
                let scroll_distance =
                    scene.scroll_offset_y as i32 - scene.last_scroll_offset_y as i32;
                let scroll_amount = scroll_distance.abs();

                // Damage is the full area by default.
                let mut damage_y0 = next_y0;
                let mut damage_y1 = next_y1;
                let mut clear_offset = 0;

                // Determine smaller damage area based on scroll direction.
                if self.size == *size && scroll_amount < height as i32 {
                    if scroll_distance >= 0 {
                        damage_y0 = (bottom - scroll_amount as u32) % buffer_height;
                        damage_y1 = bottom % buffer_height;
                        clear_offset = -(buffer_height as i32);
                    } else {
                        damage_y0 = top % buffer_height;
                        damage_y1 = (top + scroll_amount as u32) % buffer_height;
                        clear_offset = buffer_height as i32;
                    }
                }

                // Convert upper bound to height instead of 0.
                if damage_y1 == 0 {
                    damage_y1 = buffer_height;
                }

                // Compute top/bottom spans. We end up with both a top and
                // bottom span if damage wraps around.
                let mut top_y0 = damage_y0;
                let mut top_y1 = damage_y1;
                let mut bottom_y0 = damage_y1;
                let mut bottom_y1 = damage_y1;
                if damage_y0 > damage_y1 {
                    top_y0 = 0;
                    top_y1 = damage_y1;
                    bottom_y0 = damage_y0;
                    bottom_y1 = buffer_height;
                }

                // Round to multiple of 32. This ensures that we can use the
                // render clip to redraw the exposed region.
                top_y0 = top_y0 & !31;
                top_y1 = (top_y1 + 31) & !31;
                if bottom_y0 < bottom_y1 {
                    bottom_y0 = bottom_y0 & !31;
                    bottom_y1 = (bottom_y1 + 31) & !31;
                }

                let image = staging_image
                    .take()
                    .unwrap_or_else(|| context.new_image(Size2D::new(width, buffer_height)));

                // Offset in staging image that translate to top of output.
                let y_start = next_y0;
                // Offsets that needs to be applied to contents in order to
                // render at the correct location in staging image.
                let mut dy = (top / buffer_height) * buffer_height;

                // Translate intersecting paths and render bottom span if needed.
                if bottom_y0 < bottom_y1 {
                    let mut composition = Composition::new(std::iter::empty(), BACKGROUND_COLOR);
                    let clear_dy = (dy as i32 + clear_offset) as u32;
                    let viewport = Rect::new(
                        Point::new(0.0, (clear_dy + bottom_y0) as f32),
                        Size::new(width as f32, (bottom_y1 - bottom_y0) as f32),
                    );
                    scene.set_items_ty(-(clear_dy as f32));
                    scene.build_clear_rasters_for_viewport(context, &viewport);
                    scene.prepend_clear_layers_to_composition(&mut composition, &viewport);

                    let viewport = Rect::new(
                        Point::new(0.0, (dy + bottom_y0) as f32),
                        Size::new(width as f32, (bottom_y1 - bottom_y0) as f32),
                    );
                    scene.set_items_ty(-(dy as f32));
                    scene.build_rasters_for_viewport(context, &viewport);
                    scene.prepend_layers_to_composition(&mut composition, &viewport);

                    let clip = Rect::new(
                        Point2D::new(0, bottom_y0),
                        Size2D::new(width, bottom_y1 - bottom_y0),
                    );
                    context.render(&composition, Some(clip), image, &ext);

                    ext.pre_clear = None;
                }

                // Offset contents of top span if it is below top of output.
                if top_y1 <= y_start {
                    dy += buffer_height;
                }

                // Determine exposure if motion blur mode is used.
                let exposure = match self.scroll_method {
                    ScrollMethod::MotionBlur => {
                        (scroll_distance as f32 * scene.exposure).round() as i32
                    }
                    _ => 0,
                };

                // Copy temporary buffer to output image and apply motion blur.
                ext.post_copy = Some(PostCopy {
                    image: self.image,
                    color: BACKGROUND_COLOR,
                    exposure_distance: Vector2D::new(0, exposure),
                    copy_region: CopyRegion {
                        src_offset: Point2D::new(0, y_start),
                        dst_offset: Point2D::new(0, 0),
                        extent: Size2D::new(width, height),
                    },
                });

                let mut composition = Composition::new(std::iter::empty(), BACKGROUND_COLOR);

                let clear_dy = (dy as i32 + clear_offset) as u32;
                let viewport = Rect::new(
                    Point::new(0.0, (clear_dy + top_y0) as f32),
                    Size::new(width as f32, (top_y1 - top_y0) as f32),
                );
                scene.set_items_ty(-(clear_dy as f32));
                scene.build_clear_rasters_for_viewport(context, &viewport);
                scene.prepend_clear_layers_to_composition(&mut composition, &viewport);

                // Translate intersecting paths and render top span.
                let viewport = Rect::new(
                    Point::new(0.0, (dy + top_y0) as f32),
                    Size::new(width as f32, (top_y1 - top_y0) as f32),
                );
                scene.set_items_ty(-(dy as f32));
                scene.build_rasters_for_viewport(context, &viewport);
                scene.prepend_layers_to_composition(&mut composition, &viewport);

                let clip = Rect::new(Point2D::new(0, top_y0), Size2D::new(width, top_y1 - top_y0));
                context.render(&composition, Some(clip), image, &ext);

                staging_image.replace(image);
            }
        }

        context.flush_image(self.image);

        self.size = *size;
        self.scroll_offset_y = scene.scroll_offset_y;
    }
}

// TODO: Remove touch device when supported by carnelian.
struct TouchDevice {
    x_range: hid_input_report::Range,
    y_range: hid_input_report::Range,
    receiver: UnboundedReceiver<hid_input_report::InputReport>,
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

            let descriptor = device.get_descriptor(Time::INFINITE)?;
            match descriptor.touch {
                None => continue,
                Some(touch) => {
                    println!("touch device: {0}", entry.path().to_str().unwrap());
                    let input_descriptor = &touch.input.unwrap();
                    let contact_descriptor = &input_descriptor.contacts.as_ref().unwrap()[0];
                    let x_range = contact_descriptor.position_x.as_ref().unwrap().range;
                    let y_range = contact_descriptor.position_y.as_ref().unwrap().range;
                    let (sender, receiver) =
                        futures::channel::mpsc::unbounded::<hid_input_report::InputReport>();

                    // TODO: move direct input processing into carnelian.
                    fasync::spawn_local(async move {
                        if let Ok((_, event)) = device.get_reports_event(Time::INFINITE) {
                            while let Ok(_) =
                                fasync::OnSignals::new(&event, zx::Signals::USER_0).await
                            {
                                if let Ok(reports) = device.get_reports(Time::INFINITE) {
                                    for report in reports {
                                        let event_time =
                                            report.event_time.expect("missing timestamp");
                                        duration!("input", "on_report", "event_time" => event_time);
                                        sender.unbounded_send(report).expect("unbounded_send");
                                    }
                                }
                            }
                        }
                    });

                    return Ok(TouchDevice { x_range, y_range, receiver });
                }
            }
        }
        Err(std::io::Error::new(std::io::ErrorKind::NotFound, "no touch device found").into())
    }

    fn get_report(&mut self) -> Option<hid_input_report::InputReport> {
        self.receiver.try_next().unwrap_or(None)
    }
}

struct FlingCurve {
    curve_duration: f32,
    start_timestamp: Time,
    displacement_ratio: Vector2D<f32>,
    cumulative_scroll: Vector2D<f32>,
    previous_timestamp: Time,
    time_offset: f32,
    position_offset: f32,
}

// Alpha-beta-gamma filter constants taken from the Chromium project.
// These constants come from UX experiments.
const ALPHA: f32 = -5.70762e+03;
const BETA: f32 = 1.72e+02;
const GAMMA: f32 = 3.7e+00;

impl FlingCurve {
    fn get_position_at_time(t: f32) -> f32 {
        ALPHA * (-GAMMA * t).exp() - BETA * t - ALPHA
    }

    fn get_velocity_at_time(t: f32) -> f32 {
        -ALPHA * GAMMA * (-GAMMA * t).exp() - BETA
    }

    fn get_time_at_velocity(v: f32) -> f32 {
        -((v + BETA) / (-ALPHA * GAMMA)).ln() / GAMMA
    }

    fn new(velocity: Vector2D<f32>, start_timestamp: Time) -> Self {
        let max_start_velocity =
            velocity.x.abs().max(velocity.y.abs()).min(Self::get_velocity_at_time(0.0));
        assert!(max_start_velocity > 0.0);
        let displacement_ratio = velocity / max_start_velocity;
        let time_offset = Self::get_time_at_velocity(max_start_velocity);
        let position_offset = Self::get_position_at_time(time_offset);
        let curve_duration = Self::get_time_at_velocity(0.0);

        FlingCurve {
            curve_duration,
            start_timestamp,
            displacement_ratio,
            cumulative_scroll: Vector2D::zero(),
            previous_timestamp: start_timestamp,
            time_offset,
            position_offset,
        }
    }

    fn compute_scroll_offset(&mut self, timestamp: Time) -> (bool, Vector2D<f32>, Vector2D<f32>) {
        let elapsed_time = timestamp - self.start_timestamp;
        if elapsed_time < zx::Duration::from_nanos(0) {
            return (true, Vector2D::zero(), Vector2D::zero());
        }

        const SECONDS_PER_NANOSECOND: f32 = 1e-9;

        let offset_time =
            elapsed_time.into_nanos() as f32 * SECONDS_PER_NANOSECOND + self.time_offset;
        let (still_active, scalar_offset, scalar_velocity) = if offset_time < self.curve_duration {
            (
                true,
                Self::get_position_at_time(offset_time) - self.position_offset,
                Self::get_velocity_at_time(offset_time),
            )
        } else {
            (false, Self::get_position_at_time(self.curve_duration) - self.position_offset, 0.0)
        };

        (
            still_active,
            self.displacement_ratio * scalar_offset,
            self.displacement_ratio * scalar_velocity,
        )
    }

    fn compute_scroll_delta_at_time(&mut self, current: Time) -> (bool, Vector2D<f32>) {
        if current <= self.previous_timestamp {
            (true, Vector2D::zero())
        } else {
            self.previous_timestamp = current;
            let (still_active, offset, _) = self.compute_scroll_offset(current);
            let delta = offset - self.cumulative_scroll;
            self.cumulative_scroll = offset;
            (still_active, delta)
        }
    }
}

struct InfiniteScrollViewAssistant<B: Backend, C: Context<B>> {
    context: C,
    scroll_method: ScrollMethod,
    touch_sampling_offset: zx::Duration,
    scene: Scene<B>,
    contents: BTreeMap<u64, Contents<B>>,
    last_presentation_time: Time,
    touch_device: Option<TouchDevice>,
    touch_points: Vec<Point>,
    touch_time: Time,
    previous_touch_points: Vec<Point>,
    previous_touch_time: Time,
    scroll_origin: Vector2D<f32>,
    fling_curve: Option<FlingCurve>,
    fake_scroll_start: Time,
    fake_scroll_velocity: Option<f32>,
    staging_image: Option<B::Image>,
}

impl<B: Backend, C: Context<B>> InfiniteScrollViewAssistant<B, C> {
    pub fn new(
        context: C,
        scale: f32,
        scroll_method: ScrollMethod,
        exposure: f32,
        touch_sampling_offset: zx::Duration,
        max_columns: u32,
        text_disabled: bool,
    ) -> Self {
        let scene = Scene::new(scale, exposure, max_columns, text_disabled);
        let touch_device = TouchDevice::create().ok();
        let fake_scroll_start =
            Time::from_nanos(Time::get(ClockId::Monotonic).into_nanos().saturating_add(
                zx::Duration::from_seconds(FAKE_SCROLL_DELAY_SECONDS).into_nanos(),
            ));

        Self {
            context,
            scroll_method,
            touch_sampling_offset,
            scene,
            contents: BTreeMap::new(),
            last_presentation_time: Time::get(ClockId::Monotonic),
            touch_device,
            touch_points: Vec::new(),
            touch_time: Time::from_nanos(0),
            previous_touch_points: Vec::new(),
            previous_touch_time: Time::from_nanos(0),
            scroll_origin: Vector2D::zero(),
            fling_curve: None,
            fake_scroll_start,
            fake_scroll_velocity: None,
            staging_image: None,
        }
    }
}

impl<B: Backend, C: Context<B>> ViewAssistant for InfiniteScrollViewAssistant<B, C> {
    fn setup(&mut self, _context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        duration!("gfx", "InfiniteScrollViewAssistant::update");

        let canvas = context.canvas.as_ref().unwrap().borrow();
        let size = &context.size;
        let presentation_time = context.presentation_time;
        let elapsed = presentation_time - self.last_presentation_time;
        let scroll_method = self.scroll_method;

        // Scroll distance is set by input processing code below.
        let mut scroll_distance = None;

        // Process touch device input.
        //
        // Smooth motion is achieved by using a fixed offset from the
        // presentation time and approximating the touch location at
        // the exact sampling time.
        if let Some(device) = self.touch_device.as_mut() {
            // Determine sample time by removing sampling offset from presentation time.
            let sample_time = presentation_time - self.touch_sampling_offset;

            fn average(points: &Vec<Point>) -> Vector2D<f32> {
                points.iter().fold(Vector2D::zero(), |sum, x| sum + x.to_vector())
                    / points.len() as f32
            }

            // Read new reports until we have one report past the sample time.
            while self.touch_time <= sample_time {
                match device.get_report() {
                    Some(report) => {
                        let touch = report.touch.as_ref();
                        let contacts = touch.unwrap().contacts.as_ref().unwrap();

                        // Save previous touch state.
                        self.previous_touch_time = self.touch_time;
                        self.previous_touch_points.clear();
                        self.previous_touch_points.extend(self.touch_points.drain(..));
                        // Update touch points and origin. Origin is the average of
                        // all touch points.
                        for contact in contacts.iter() {
                            let point = Point::new(
                                size.width * contact.position_x.unwrap() as f32
                                    / device.x_range.max as f32,
                                size.height * contact.position_y.unwrap() as f32
                                    / device.y_range.max as f32,
                            );
                            self.touch_points.push(point);
                        }
                        counter!("input", "touch_y", 0, "y" => (average(&self.touch_points)).y.round() as u32);

                        // Update touch time.
                        self.touch_time = Time::from_nanos(report.event_time.unwrap());

                        // Ignore previous touch points and set scroll origin if number of
                        // touch points changed.
                        if self.previous_touch_points.len() != self.touch_points.len() {
                            self.scroll_origin = average(&self.touch_points);
                            self.previous_touch_points.clear();
                        }
                    }
                    // Stop when no more reports are available.
                    _ => break,
                }
            }

            if !self.touch_points.is_empty() && !self.previous_touch_points.is_empty() {
                let origin = average(&self.touch_points);
                // Approximate origin at the sample time by assuming a linear
                // change to touch location over the sampling interval.
                if self.touch_time > sample_time && self.touch_time > self.previous_touch_time {
                    let interval = (self.touch_time - self.previous_touch_time).into_nanos() as f32;
                    let scalar =
                        (sample_time - self.previous_touch_time).into_nanos() as f32 / interval;
                    let previous_origin = average(&self.previous_touch_points);
                    let estimated_origin = previous_origin + (origin - previous_origin) * scalar;
                    scroll_distance = Some(-(estimated_origin.y - self.scroll_origin.y));
                } else {
                    let touch_latency = presentation_time - self.touch_time;
                    // Print a warning if touch sampling offset is too low for
                    // accurate touch point sampling.
                    if touch_latency > self.touch_sampling_offset {
                        println!(
                            "warning: high touch latency {:?} ms",
                            touch_latency.into_nanos() as f32 / 1e+6
                        );
                    }
                    scroll_distance = Some(-(origin.y - self.scroll_origin.y));
                }

                // Delay the start of fake scrolling.
                self.fake_scroll_start =
                    Time::from_nanos(presentation_time.into_nanos().saturating_add(
                        zx::Duration::from_seconds(FAKE_SCROLL_DELAY_SECONDS).into_nanos(),
                    ));
                self.fake_scroll_velocity = None;
            }
        }

        const SECONDS_PER_NANOSECOND: f32 = 1e-9;

        // Fake scroll when idle.
        if presentation_time.into_nanos() > self.fake_scroll_start.into_nanos() {
            const MIN_FAKE_SPEED: f32 = 2048.0;
            const MAX_FAKE_SPEED: f32 = 8192.0;
            const FAKE_DIRECTIONS: [f32; 2] = [1.0, -1.0];

            let fake_scroll_velocity = self.fake_scroll_velocity.get_or_insert_with(|| {
                let mut rng = thread_rng();
                let speed: f32 = rng.gen_range(MIN_FAKE_SPEED, MAX_FAKE_SPEED);
                let direction: usize = rng.gen_range(0, FAKE_DIRECTIONS.len());
                speed * FAKE_DIRECTIONS[direction]
            });

            const ACCELERATION: f32 = 12000.0;
            let elapsed_seconds = (presentation_time.into_nanos()
                - self.fake_scroll_start.into_nanos()) as f32
                * SECONDS_PER_NANOSECOND;
            let velocity = (ACCELERATION * elapsed_seconds).min(fake_scroll_velocity.abs())
                * fake_scroll_velocity.signum();

            scroll_distance =
                Some(velocity * (elapsed.into_nanos() as f32 * SECONDS_PER_NANOSECOND));

            // Fake scroll last for one second. Add small delay before next
            // fake scroll starts.
            if elapsed_seconds > 1.0 {
                const NEXT_FAKE_SCROLL_DELAY_SECONDS: i64 = 2;
                self.fake_scroll_start =
                    Time::from_nanos(presentation_time.into_nanos().saturating_add(
                        zx::Duration::from_seconds(NEXT_FAKE_SCROLL_DELAY_SECONDS).into_nanos(),
                    ));
                self.fake_scroll_velocity = None;
            }
        }

        // Calculate scroll delta based on scroll distance and update fling
        // curve. If scroll distance is not set then allow fling curve to
        // determine scroll delta.
        let mut scroll_delta = 0;
        if let Some(distance) = scroll_distance {
            scroll_delta = distance.round() as i32;
            self.scroll_origin.y -= scroll_delta as f32;
            if distance != 0.0 && elapsed.into_nanos() > 0 {
                let velocity = distance / (elapsed.into_nanos() as f32 * SECONDS_PER_NANOSECOND);
                self.fling_curve =
                    Some(FlingCurve::new(Vector2D::new(0.0, velocity), presentation_time));
            }
        } else if let Some(fling) = &mut self.fling_curve {
            let (fling_active, delta) = fling.compute_scroll_delta_at_time(presentation_time);
            scroll_delta = delta.y as i32;
            if !fling_active {
                self.fling_curve = None;
            }
        }

        // Update the scene using our scroll delta.
        self.scene.update(&mut self.context, size, scroll_delta);

        // Temporary hack to deal with the fact that carnelian
        // allocates a new buffer for each frame with the same
        // image ID of zero.
        let mut temp_content;
        let content;
        let image = self.context.get_current_image(context);

        if canvas.id == 0 {
            temp_content = Contents::new(image, scroll_method);
            content = &mut temp_content;
        } else {
            content = self
                .contents
                .entry(canvas.id)
                .or_insert_with(|| Contents::new(image, scroll_method));
        }
        content.update(&mut self.context, &mut self.scene, size, &mut self.staging_image);

        self.last_presentation_time = presentation_time;

        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::EveryFrame;
    }

    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat {
        self.context.pixel_format()
    }
}

fn main() -> Result<(), Error> {
    // TODO: Remove this trace provider when carnelian supports tracing.
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    App::run(make_app_assistant::<InfiniteScrollAppAssistant>())
}
