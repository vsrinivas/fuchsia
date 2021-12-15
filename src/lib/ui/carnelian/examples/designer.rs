// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    carnelian::{
        color::Color,
        input::{self},
        make_app_assistant,
        render::{
            BlendMode, Context as RenderContext, Fill, FillRule, Layer, Path, PathBuilder, Style,
        },
        scene::{
            facets::Facet,
            scene::{Scene, SceneBuilder, SceneOrder},
            LayerGroup,
        },
        App, AppAssistant, Point, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr,
        ViewKey,
    },
    core::cell::RefCell,
    euclid::{point2, size2},
    fuchsia_trace_provider,
    fuchsia_zircon::Event,
    std::collections::HashSet,
    std::collections::VecDeque,
    std::convert::TryFrom,
    std::f32,
    std::rc::Rc,
};

const BACKGROUND_COLOR: Color = Color { r: 235, g: 255, b: 255, a: 255 };

#[derive(Default)]
struct DesignerAppAssistant;

impl AppAssistant for DesignerAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(DesignerViewAssistant::new()))
    }
}

struct DesignerFacet {
    size: Size,
    designer_state: Rc<RefCell<DesignerState>>,
}

impl DesignerFacet {
    fn new(designer_state: Rc<RefCell<DesignerState>>) -> Self {
        Self { size: size2(1.0, 1.0), designer_state }
    }
}

impl Facet for DesignerFacet {
    fn update_layers(
        &mut self,
        size: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        _view_context: &ViewAssistantContext,
    ) -> std::result::Result<(), anyhow::Error> {
        self.size = size;
        self.designer_state.borrow_mut().update_layer_group(render_context, layer_group);
        Ok(())
    }

    fn calculate_size(&self, _available: Size) -> Size {
        self.size
    }
}

struct SceneDetails {
    scene: Scene,
}

struct DesignerViewAssistant {
    scene_details: Option<SceneDetails>,
    designer_state: Rc<RefCell<DesignerState>>,
    pointers: HashSet<input::pointer::PointerId>,
}

impl DesignerViewAssistant {
    pub fn new() -> Self {
        Self {
            scene_details: None,
            designer_state: Rc::new(RefCell::new(DesignerState::default())),
            pointers: HashSet::new(),
        }
    }
}

impl ViewAssistant for DesignerViewAssistant {
    fn resize(&mut self, _new_size: &Size) -> Result<(), Error> {
        self.scene_details = None;
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let mut scene_details = self.scene_details.take().unwrap_or_else(|| {
            let mut builder = SceneBuilder::new().background_color(BACKGROUND_COLOR).mutable(false);
            let designer_facet = DesignerFacet::new(Rc::clone(&self.designer_state));
            let _ = builder.facet(Box::new(designer_facet));
            SceneDetails { scene: builder.build() }
        });

        scene_details.scene.render(render_context, ready_event, context)?;

        self.scene_details = Some(scene_details);

        context.request_render();

        Ok(())
    }

    fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext,
        _event: &input::Event,
        pointer_event: &input::pointer::Event,
    ) -> Result<(), Error> {
        match &pointer_event.phase {
            input::pointer::Phase::Down(touch_location) => {
                self.designer_state
                    .borrow_mut()
                    .push_event(InputEvent::Down(touch_location.to_f32()));
                self.pointers.insert(pointer_event.pointer_id.clone());
                context.request_render();
            }
            input::pointer::Phase::Moved(touch_location)
                if self.pointers.get(&pointer_event.pointer_id).is_some() =>
            {
                self.designer_state
                    .borrow_mut()
                    .push_event(InputEvent::Moved(touch_location.to_f32()));
                context.request_render();
            }
            input::pointer::Phase::Up => {
                self.designer_state.borrow_mut().push_event(InputEvent::Up);
                context.request_render();
            }
            _ => (),
        }
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    println!("Designer Example");
    App::run(make_app_assistant::<DesignerAppAssistant>())
}

#[derive(PartialEq, Debug)]
enum Bezier {
    Quadratic,
    Cubic,
}

#[derive(PartialEq, Debug)]
enum DrawingMode {
    Point,
    Line,
    PreCurve,
    Curve(Bezier),
}

fn quadratic_bezier(t: f32, p0: Point, p1: Point, p2: Point) -> Point {
    lerp(t, lerp(t, p0, p1), lerp(t, p1, p2))
}

fn cubic_bezier(t: f32, p0: Point, p1: Point, p2: Point, p3: Point) -> Point {
    lerp(t, quadratic_bezier(t, p0, p1, p2), quadratic_bezier(t, p1, p2, p3))
}

/// Draw bezier curve anchors with the path builder.
///
/// * `first_anchor` - First curve anchor.
/// * `second_anchor` - Second curve anchor
/// * `p3` - Middle point.
/// * `path_builder` - The path builder.
fn draw_anchors(
    first_anchor: Point,
    second_anchor: Point,
    p3: Point,
    path_builder: &mut PathBuilder,
) {
    for p in [second_anchor, first_anchor, p3] {
        draw_point(p, path_builder);
    }
    // draw_line(first_anchor, second_anchor, path_builder);
}

/// Draw a cubic bezier curve with the path builder.
///
/// * `p0` - Starting point.
/// * `p1` - First anchor point.
/// * `p2` - Second anchor point.
/// * `p3` - Ending point.
/// * `path_builder` - The path builder.
fn draw_cubic_bezier_curve(
    p0: Point,
    p1: Point,
    p2: Point,
    p3: Point,
    path_builder: &mut PathBuilder,
) {
    // Let's say we want to have 100 segments for every curve.
    let mut last_point = p0;
    for t in 1..100 {
        let point = cubic_bezier((t as f32) * 0.01, p0, p1, p2, p3);
        let (p0_n, p1_n, p2_n, p3_n) = compute_rec(last_point, point);
        draw_rec(p2_n, p3_n, p1_n, p0_n, path_builder);
        last_point = point;
    }
}

/// Draw a quadratic bezier curve with the path builder.
///
/// * `p0` - Starting point.
/// * `p1` - Anchor point.
/// * `p2` - Ending point.
/// * `path_builder` - The path builder.
fn draw_quadratic_bezier_curve(p0: Point, p1: Point, p2: Point, path_builder: &mut PathBuilder) {
    // Let's say we want to have 100 segments for every curve.
    let mut last_point = p0;
    for t in 1..100 {
        let point = quadratic_bezier((t as f32) * 0.01, p0, p1, p2);
        let (p0_n, p1_n, p2_n, p3_n) = compute_rec(last_point, point);
        draw_rec(p2_n, p3_n, p1_n, p0_n, path_builder);
        last_point = point;
    }
}

/// Draw a line with the path builder
///
/// * `p0` - Starting point.
/// * `p1` - Ending point.
/// * `path_builder` - The path builder.
fn draw_line(p0: Point, p1: Point, path_builder: &mut PathBuilder) {
    let (p0_n, p1_n, p2_n, p3_n) = compute_rec(p0, p1);

    draw_rec(p2_n, p3_n, p1_n, p0_n, path_builder);
}

/// Draw a point with the path builder.
///
/// The point will be drawn as a square with a width of 3px.
///
/// * `p` - Point position.
/// * `path_builder` - The path builder.
fn draw_point(p: Point, path_builder: &mut PathBuilder) {
    let p0 = Point::new(p.x - 3.0, p.y + 3.0);
    let p1 = Point::new(p.x + 3.0, p.y + 3.0);
    let p2 = Point::new(p.x + 3.0, p.y - 3.0);
    let p3 = Point::new(p.x - 3.0, p.y - 3.0);

    draw_rec(p0, p1, p2, p3, path_builder);
}

/// Draw a rectangle on the given path.
///
/// * `p0` - Top-left point.
/// * `p1` - Top-right point.
/// * `p2` - Bottom-right point.
/// * `p3` - Bottom-left point.
/// * `path_builder` - The path builder.
fn draw_rec(p0: Point, p1: Point, p2: Point, p3: Point, path_builder: &mut PathBuilder) {
    path_builder.move_to(p0);
    // p'____p1
    // x____x
    path_builder.line_to(p1);
    // x____p'
    // x____p2
    path_builder.line_to(p2);
    // x____x
    // p3____p'
    path_builder.line_to(p3);
    // p0____x
    // p'____x
    path_builder.line_to(p0);
}

/// Draw a rectangle around two points.
///
/// * `p0` - First point.
/// * `p1` - Second point.
fn compute_rec(p0: Point, p1: Point) -> (Point, Point, Point, Point) {
    let vector_x = p1.x - p0.x;
    let vector_y = p1.y - p0.y;

    let perpendicular_x = vector_y;
    let perpendicular_y = -vector_x;

    let length = (perpendicular_x * perpendicular_x + perpendicular_y * perpendicular_y).sqrt();

    let normalize_x = perpendicular_x / length;
    let normalize_y = perpendicular_y / length;

    let width = 2.0;

    let p0_n = Point::new(p0.x + normalize_x * width / 2.0, p0.y + normalize_y * width / 2.0);
    let p1_n = Point::new(p0.x - normalize_x * width / 2.0, p0.y - normalize_y * width / 2.0);
    let p2_n = Point::new(p1.x + normalize_x * width / 2.0, p1.y + normalize_y * width / 2.0);
    let p3_n = Point::new(p1.x - normalize_x * width / 2.0, p1.y - normalize_y * width / 2.0);

    (p0_n, p1_n, p2_n, p3_n)
}

fn lerp(t: f32, p0: Point, p1: Point) -> Point {
    point2(p0.x * (1.0 - t) + p1.x * t, p0.y * (1.0 - t) + p1.y * t)
}

/// Reflect a point against another point.
///
/// * `p0` - Point to reflect.
/// * `p1` - Reference point.
fn reflect_point(p0: Point, p1: Point) -> Point {
    point2(2.0 * p1.x - p0.x, 2.0 * p1.y - p0.y)
}

#[derive(Debug)]
struct PairQueue<T> {
    queue: VecDeque<T>,
}

impl<T> PairQueue<T> {
    pub fn new() -> Self {
        PairQueue { queue: VecDeque::new() }
    }

    pub fn len(&self) -> usize {
        self.queue.len()
    }

    pub fn push(&mut self, item: T) {
        self.queue.push_front(item);
        if self.len() > 2 {
            self.pop();
        }
    }

    fn pop(&mut self) {
        self.queue.pop_back();
    }

    pub fn first(&self) -> Option<&T> {
        self.queue.front()
    }

    pub fn second(&self) -> Option<&T> {
        self.queue.back()
    }
}

#[derive(Debug)]
struct MouseDesignerState {
    pub pos: Point,
    pub is_dragged: bool,
    pub is_pressed: bool,
}

#[derive(Debug, Default)]
struct Cursor {
    pub is_dragged: bool,
    pub position: Option<Point>,
    pub is_pressed: bool,
}

impl Cursor {
    pub fn push_event(&mut self, input_event: MouseDesignerState) {
        self.position = Some(input_event.pos);
        self.is_dragged = input_event.is_dragged;
        self.is_pressed = input_event.is_pressed;
    }
}

#[derive(Debug)]
enum InputEvent {
    Down(Point),
    Moved(Point),
    Up,
}

#[derive(Debug)]
enum MouseUpAnchorEvent {
    SetLastAnchor(Point),
    ResetLastAnchor,
}

#[derive(Debug)]
struct DesignerState {
    pub cursor: Cursor,
    pub drawing_mode: DrawingMode,
    pub last_points: PairQueue<Point>,
    pub last_anchor: Option<Point>,
    pub layer_order: SceneOrder,
}

impl DesignerState {
    fn handle_mouse_up_anchor_event(&self) -> MouseUpAnchorEvent {
        if matches!(self.drawing_mode, DrawingMode::Curve(_)) {
            MouseUpAnchorEvent::SetLastAnchor(self.cursor.position.unwrap())
        } else {
            MouseUpAnchorEvent::ResetLastAnchor
        }
    }

    fn handle_mouse_release(&mut self) {
        self.layer_order =
            SceneOrder::try_from(self.layer_order.as_u32() + 1).unwrap_or_else(|e| panic!("{}", e));
        self.last_anchor = match self.handle_mouse_up_anchor_event() {
            MouseUpAnchorEvent::SetLastAnchor(position) => Some(position),
            MouseUpAnchorEvent::ResetLastAnchor => None,
        };

        self.layer_order = SceneOrder::try_from(
            self.layer_order.as_u32()
                + match self.drawing_mode {
                    DrawingMode::Curve(_) => 3,
                    DrawingMode::Line | DrawingMode::PreCurve => 2,
                    DrawingMode::Point => 1,
                },
        )
        .unwrap_or_else(|e| panic!("{}", e));

        if matches!(self.drawing_mode, DrawingMode::Curve(_) | DrawingMode::PreCurve) {
            self.drawing_mode = DrawingMode::Line;
        };
    }

    fn handle_mouse_press(&mut self) {
        if !matches!(self.drawing_mode, DrawingMode::Curve(_) | DrawingMode::PreCurve) {
            self.last_points.push(self.cursor.position.unwrap());
        }
    }

    pub fn push_event(&mut self, input_event: InputEvent) {
        match input_event {
            InputEvent::Down(pos) => {
                self.cursor.push_event(MouseDesignerState {
                    pos,
                    is_dragged: false,
                    is_pressed: true,
                });
                self.handle_mouse_press();
            }
            InputEvent::Up => {
                self.cursor.push_event(MouseDesignerState {
                    pos: self.cursor.position.unwrap(),
                    is_dragged: false,
                    is_pressed: false,
                });
                self.handle_mouse_release();
            }
            InputEvent::Moved(pos) => {
                self.cursor.push_event(MouseDesignerState {
                    pos,
                    is_dragged: true,
                    is_pressed: true,
                });
            }
        };
    }

    fn insert_layer_in_group(
        &self,
        path: Path,
        context: &mut RenderContext,
        layer_group: &mut dyn LayerGroup,
        layer_order: SceneOrder,
    ) {
        let mut raster_builder = context.raster_builder().expect("raster_builder");
        raster_builder.add(&path, None);
        let raster = raster_builder.build();
        layer_group.insert(
            layer_order,
            Layer {
                raster,
                clip: None,
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(Color { r: 0, g: 0, b: 0, a: 255 }),
                    blend_mode: BlendMode::Over,
                },
            },
        );
    }

    pub fn update_layer_group(
        &mut self,
        context: &mut RenderContext,
        layer_group: &mut dyn LayerGroup,
    ) {
        match self.drawing_mode {
            DrawingMode::Point if self.cursor.is_pressed => {
                let p0 = self.last_points.first().unwrap();

                let mut path_builder = context.path_builder().unwrap();

                draw_point(*p0, &mut path_builder);

                let path = path_builder.build();
                self.insert_layer_in_group(path, context, layer_group, self.layer_order);

                self.cursor.is_pressed = false;
                self.drawing_mode = DrawingMode::Line;
            }
            DrawingMode::Line if self.cursor.is_pressed && self.last_points.len() == 2 => {
                let p0 = self.last_points.second().unwrap();
                let p1 = self.last_points.first().unwrap();

                for i in 0..2 {
                    let mut path_builder = context.path_builder().unwrap();
                    match i {
                        0 => draw_line(*p0, *p1, &mut path_builder),
                        1 => draw_point(*p1, &mut path_builder),
                        _ => (),
                    }
                    let path = path_builder.build();
                    self.insert_layer_in_group(
                        path,
                        context,
                        layer_group,
                        SceneOrder::try_from(self.layer_order.as_u32() + i as u32)
                            .unwrap_or_else(|e| panic!("{}", e)),
                    );
                }

                self.drawing_mode = DrawingMode::PreCurve;
            }
            DrawingMode::PreCurve => {
                self.drawing_mode = if self.last_anchor.is_some() {
                    DrawingMode::Curve(Bezier::Cubic)
                } else {
                    DrawingMode::Curve(Bezier::Quadratic)
                }
            }
            DrawingMode::Curve(Bezier::Quadratic) if self.cursor.is_dragged => {
                let p1 = self.cursor.position.unwrap();
                let p3 = self.last_points.first().unwrap();
                let p0 = self.last_points.second().unwrap();
                let p2 = reflect_point(p1, *p3);

                for i in 0..3 {
                    let mut path_builder = context.path_builder().unwrap();
                    match i {
                        0 => draw_quadratic_bezier_curve(*p0, p2, *p3, &mut path_builder),
                        1 => draw_line(p1, p2, &mut path_builder),
                        2 => draw_anchors(p1, p2, *p3, &mut path_builder),
                        _ => (),
                    }
                    let path = path_builder.build();
                    self.insert_layer_in_group(
                        path,
                        context,
                        layer_group,
                        SceneOrder::try_from(self.layer_order.as_u32() + i as u32)
                            .unwrap_or_else(|e| panic!("{}", e)),
                    );
                }
            }
            DrawingMode::Curve(Bezier::Cubic) if self.cursor.is_dragged => {
                let p1 = self.cursor.position.unwrap();
                let p3 = self.last_points.first().unwrap();
                let p0 = self.last_points.second().unwrap();
                let p2 = reflect_point(p1, *p3);
                let anchor = self.last_anchor.unwrap();

                for i in 0..3 {
                    let mut path_builder = context.path_builder().unwrap();
                    match i {
                        0 => draw_cubic_bezier_curve(*p0, anchor, p2, *p3, &mut path_builder),
                        1 => draw_line(p1, p2, &mut path_builder),
                        2 => draw_anchors(p1, p2, *p3, &mut path_builder),
                        _ => (),
                    }
                    let path = path_builder.build();
                    self.insert_layer_in_group(
                        path,
                        context,
                        layer_group,
                        SceneOrder::try_from(self.layer_order.as_u32() + i as u32)
                            .unwrap_or_else(|e| panic!("{}", e)),
                    );
                }
            }
            _ => (),
        }
    }
}

impl Default for DesignerState {
    fn default() -> Self {
        Self {
            cursor: Cursor::default(),
            drawing_mode: DrawingMode::Point,
            last_points: PairQueue::new(),
            last_anchor: None,
            layer_order: SceneOrder::default(),
        }
    }
}
