// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::{
    color::Color,
    drawing::{load_font, path_for_rectangle, FontFace},
    input::{self},
    make_app_assistant,
    render::{BlendMode, Context as RenderContext, Fill, FillRule, Layer, Raster, Style},
    scene::{
        facets::{
            Facet, FacetId, TextFacetOptions, TextHorizontalAlignment, TextVerticalAlignment,
        },
        group::GroupMemberData,
        layout::{
            Alignment, CrossAxisAlignment, FlexMemberData, MainAxisAlignment, MainAxisSize,
            StackMemberDataBuilder,
        },
        scene::{Scene, SceneBuilder, SceneOrder},
        LayerGroup,
    },
    App, AppAssistant, Coord, Point, Rect, Size, ViewAssistant, ViewAssistantContext,
    ViewAssistantPtr, ViewKey,
};
use euclid::size2;
use rand::{thread_rng, Rng};
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use toml;

#[derive(Default)]
struct LayoutsAppAssistant;

impl AppAssistant for LayoutsAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        LayoutsViewAssistant::new()
    }
}

const ALIGNS: &[fn() -> Alignment] = &[
    Alignment::top_left,
    Alignment::top_center,
    Alignment::top_right,
    Alignment::center_left,
    Alignment::center,
    Alignment::center_right,
    Alignment::bottom_left,
    Alignment::bottom_center,
    Alignment::bottom_right,
];

const MAIN_ALIGNS: &[MainAxisAlignment] = &[
    MainAxisAlignment::Start,
    MainAxisAlignment::End,
    MainAxisAlignment::Center,
    MainAxisAlignment::SpaceBetween,
    MainAxisAlignment::SpaceAround,
    MainAxisAlignment::SpaceEvenly,
];

const CROSS_ALIGNS: &[CrossAxisAlignment] =
    &[CrossAxisAlignment::Start, CrossAxisAlignment::End, CrossAxisAlignment::Center];

const MAIN_SIZES: &[MainAxisSize] = &[MainAxisSize::Min, MainAxisSize::Max];

const TEXT_H_ALIGNS: &[TextHorizontalAlignment] = &[
    TextHorizontalAlignment::Left,
    TextHorizontalAlignment::Center,
    TextHorizontalAlignment::Right,
];

const TEXT_V_ALIGNS: &[TextVerticalAlignment] =
    &[TextVerticalAlignment::Top, TextVerticalAlignment::Center, TextVerticalAlignment::Bottom];

struct SceneDetails {
    scene: Scene,
}

enum Mode {
    Stack(usize),
    Flex(usize, usize, usize),
    Button,
    OneThirdTwoThird,
    OneThirdTwoThirdNoCol,
    Text(usize, usize, usize),
}

impl Mode {
    pub fn next(&self) -> Self {
        match self {
            Mode::Stack(..) => Mode::Flex(0, 0, 0),
            Mode::Flex(..) => Mode::Button,
            Mode::Button => Mode::OneThirdTwoThird,
            Mode::OneThirdTwoThird => Mode::OneThirdTwoThirdNoCol,
            Mode::OneThirdTwoThirdNoCol => Mode::Text(0, 0, 0),
            Mode::Text(..) => Mode::Stack(0),
        }
    }

    pub fn previous(&self) -> Self {
        match self {
            Mode::Stack(..) => Mode::Text(0, 0, 0),
            Mode::Flex(..) => Mode::Stack(0),
            Mode::Button => Mode::Flex(0, 0, 0),
            Mode::OneThirdTwoThird => Mode::Button,
            Mode::OneThirdTwoThirdNoCol => Mode::OneThirdTwoThird,
            Mode::Text(..) => Mode::OneThirdTwoThird,
        }
    }
}

impl Default for Mode {
    fn default() -> Self {
        Self::Stack(0)
    }
}

struct LayoutsViewAssistant {
    face: FontFace,
    scene_details: Option<SceneDetails>,
    mode: Mode,
    background: bool,
}

#[derive(Serialize, Deserialize)]
struct BoundsHolder {
    bounds: Vec<Rect>,
}

impl LayoutsViewAssistant {
    fn new() -> Result<ViewAssistantPtr, Error> {
        let face = load_font(PathBuf::from("/pkg/data/fonts/RobotoSlab-Regular.ttf"))?;
        Ok(Box::new(LayoutsViewAssistant {
            face,
            mode: Mode::default(),
            background: false,
            scene_details: None,
        }))
    }

    fn cycle1(&mut self) {
        match &mut self.mode {
            Mode::Stack(alignment) => {
                *alignment = (*alignment + 1) % ALIGNS.len();
            }
            Mode::Flex(main_size, ..) => {
                *main_size = (*main_size + 1) % MAIN_SIZES.len();
            }
            Mode::Text(h, ..) => {
                *h = (*h + 1) % TEXT_H_ALIGNS.len();
            }
            _ => (),
        }
        self.refresh();
    }

    fn cycle2(&mut self) {
        match &mut self.mode {
            Mode::Flex(_, main_align, ..) => {
                *main_align = (*main_align + 1) % MAIN_ALIGNS.len();
            }
            Mode::Text(_, v, ..) => {
                *v = (*v + 1) % TEXT_V_ALIGNS.len();
            }
            _ => (),
        }
        self.refresh();
    }

    fn cycle3(&mut self) {
        match &mut self.mode {
            Mode::Flex(_, _, cross_align) => {
                *cross_align = (*cross_align + 1) % CROSS_ALIGNS.len();
            }
            Mode::Text(_, _, visual) => {
                *visual = (*visual + 1) % 4;
            }
            _ => (),
        }
        self.refresh();
    }

    fn cycle_mode(&mut self, go_next: bool) {
        self.mode = if go_next { self.mode.next() } else { self.mode.previous() };
        self.refresh();
    }

    fn refresh(&mut self) {
        self.scene_details = None;
    }

    fn dump_bounds(&mut self) {
        if let Some(scene_details) = self.scene_details.as_ref() {
            let bounds = scene_details.scene.all_facet_bounds();
            let bounds_holder = BoundsHolder { bounds: bounds[1..].to_vec() };
            let toml = toml::to_string(&bounds_holder).unwrap();
            println!("let expected_text = r#\"{}\"#;", toml);
        }
    }

    fn toggle_background(&mut self) {
        self.background = !self.background;
        self.refresh();
    }
}

impl ViewAssistant for LayoutsViewAssistant {
    fn resize(&mut self, _new_size: &Size) -> Result<(), Error> {
        self.scene_details = None;
        Ok(())
    }

    fn get_scene(&mut self, size: Size) -> Option<&mut Scene> {
        let scene_details = self.scene_details.take().unwrap_or_else(|| {
            let mut builder =
                SceneBuilder::new().background_color(Color::white()).enable_mouse_cursor(false);
            builder.group().stack().expand().align(Alignment::top_center()).contents(|builder| {
                let label = match self.mode {
                    Mode::Stack(alignment) => format!("make_two_boxes_{:?}", ALIGNS[alignment]()),
                    Mode::Flex(main_size, main_align, cross_align) => format!(
                        "make_column_with_two_boxes_{:?}_{:?}_{:?}",
                        MAIN_SIZES[main_size], MAIN_ALIGNS[main_align], CROSS_ALIGNS[cross_align],
                    ),
                    Mode::Button => String::from("button"),
                    Mode::OneThirdTwoThird => String::from("one_third_two_third"),
                    Mode::OneThirdTwoThirdNoCol => String::from("one_third_two_third_no_col"),
                    Mode::Text(..) => String::from("text"),
                }
                .to_lowercase();
                builder.text(
                    self.face.clone(),
                    &label,
                    18.0,
                    Point::zero(),
                    TextFacetOptions {
                        color: Color::fuchsia(),
                        horizontal_alignment: TextHorizontalAlignment::Left,
                        vertical_alignment: TextVerticalAlignment::Top,
                        ..TextFacetOptions::default()
                    },
                );
                match self.mode {
                    Mode::Stack(alignment) => make_two_boxes(builder, ALIGNS[alignment]()),
                    Mode::Flex(main_size, main_align, cross_align) => make_column_with_two_boxes(
                        builder,
                        MAIN_SIZES[main_size],
                        MAIN_ALIGNS[main_align],
                        CROSS_ALIGNS[cross_align],
                    ),
                    Mode::Button => make_fake_button(builder),
                    Mode::OneThirdTwoThird => make_one_third_two_third(builder),
                    Mode::OneThirdTwoThirdNoCol => make_one_third_two_third_no_col(builder),
                    Mode::Text(h, v, visual) => make_text_alignment(
                        builder,
                        &self.face,
                        size,
                        TEXT_H_ALIGNS[h],
                        TEXT_V_ALIGNS[v],
                        visual,
                    ),
                }
                if self.background {
                    build_flexible_test_facet(
                        builder,
                        Size::zero(),
                        size2(Coord::MAX, Coord::MAX),
                        None,
                        StackMemberDataBuilder::new()
                            .top(50.0)
                            .left(100.0)
                            .bottom(150.0)
                            .right(200.0)
                            .build(),
                    );
                }
            });
            let scene = builder.build();
            SceneDetails { scene }
        });

        self.scene_details = Some(scene_details);
        Some(&mut self.scene_details.as_mut().unwrap().scene)
    }

    fn handle_keyboard_event(
        &mut self,
        context: &mut ViewAssistantContext,
        _event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        const M: u32 = 109;
        const CAPITAL_M: u32 = 77;
        const ONE: u32 = 49;
        const TWO: u32 = 50;
        const THREE: u32 = 51;
        const D: u32 = 100;
        const R: u32 = 114;
        const B: u32 = 98;
        if let Some(code_point) = keyboard_event.code_point {
            if keyboard_event.phase == input::keyboard::Phase::Pressed
                || keyboard_event.phase == input::keyboard::Phase::Repeat
            {
                match code_point {
                    ONE => self.cycle1(),
                    TWO => self.cycle2(),
                    THREE => self.cycle3(),
                    M => self.cycle_mode(true),
                    CAPITAL_M => self.cycle_mode(false),
                    D => self.dump_bounds(),
                    R => self.refresh(),
                    B => self.toggle_background(),
                    _ => println!("code_point = {}", code_point),
                }
                context.request_render();
            }
        }
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    App::run(make_app_assistant::<LayoutsAppAssistant>())
}

fn random_color_element() -> u8 {
    let mut rng = thread_rng();
    let e: u8 = rng.gen_range(0..128);
    e + 128
}

fn random_color() -> Color {
    Color {
        r: random_color_element(),
        g: random_color_element(),
        b: random_color_element(),
        a: 0xff,
    }
}

#[allow(unused)]
struct TestFacet {
    min_size: Size,
    max_size: Size,
    size: Size,
    color: Color,
    raster: Option<Raster>,
}

impl TestFacet {
    fn new(min_size: Size, max_size: Size, color: Option<Color>) -> Self {
        Self {
            min_size,
            max_size,
            size: min_size,
            color: color.unwrap_or_else(|| random_color()),
            raster: None,
        }
    }
}

impl Facet for TestFacet {
    fn update_layers(
        &mut self,
        size: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        _view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let desired_size = size.max(self.min_size).min(self.max_size);
        if self.size != desired_size {
            self.raster = None;
            self.size = desired_size;
        }
        let line_raster = self.raster.take().unwrap_or_else(|| {
            let line_path = path_for_rectangle(&Rect::from_size(self.size), render_context);
            let mut raster_builder = render_context.raster_builder().expect("raster_builder");
            raster_builder.add(&line_path, None);
            raster_builder.build()
        });
        let raster = line_raster.clone();
        self.raster = Some(line_raster);
        layer_group.insert(
            SceneOrder::default(),
            Layer {
                raster: raster,
                clip: None,
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(self.color),
                    blend_mode: BlendMode::Over,
                },
            },
        );
        Ok(())
    }

    fn calculate_size(&self, _: Size) -> Size {
        self.size
    }
}

fn build_test_facet_with_member_data(
    builder: &mut SceneBuilder,
    min_size: Size,
    max_size: Size,
    color: Option<Color>,
    member_data: Option<GroupMemberData>,
) -> FacetId {
    let facet = TestFacet::new(min_size, max_size, color);
    builder.facet_with_data(Box::new(facet), member_data)
}

fn build_test_facet(builder: &mut SceneBuilder, size: Size, color: Option<Color>) -> FacetId {
    build_test_facet_with_member_data(builder, size, size, color, None)
}

fn build_flexible_test_facet(
    builder: &mut SceneBuilder,
    min_size: Size,
    max_size: Size,
    color: Option<Color>,
    member_data: Option<GroupMemberData>,
) -> FacetId {
    build_test_facet_with_member_data(builder, min_size, max_size, color, member_data)
}

const OUTER_FACET_SIZE: Size = size2(300.0, 100.0);
const INNER_FACET_SIZE: Size = size2(200.0, 50.0);

fn make_two_boxes(builder: &mut SceneBuilder, align: Alignment) {
    builder.group().stack().align(align).expand().contents(|builder| {
        let _inner = build_test_facet(builder, INNER_FACET_SIZE, Some(Color::new()));
        let _outer = build_test_facet(builder, OUTER_FACET_SIZE, Some(Color::red()));
    });
}

fn make_column_with_two_boxes(
    builder: &mut SceneBuilder,
    main_size: MainAxisSize,
    main_align: MainAxisAlignment,
    cross_align: CrossAxisAlignment,
) {
    builder
        .group()
        .column()
        .main_size(main_size)
        .main_align(main_align)
        .cross_align(cross_align)
        .contents(|builder| {
            let _inner = build_test_facet(builder, INNER_FACET_SIZE, Some(Color::new()));
            let _outer = build_test_facet(builder, OUTER_FACET_SIZE, Some(Color::red()));
        });
}

const INDICATOR_FACET_SIZE: Size = size2(80.0, 40.0);
const INDICATOR_FACET_DELTA: Size = size2(-10.0, 10.0);

fn make_fake_button(builder: &mut SceneBuilder) {
    builder.group().column().max_size().space_evenly().contents(|builder| {
        builder.group().row().max_size().space_evenly().contents(|builder| {
            let mut indicator_size = INDICATOR_FACET_SIZE;
            let _ = build_test_facet(builder, indicator_size, Some(Color::new()));
            indicator_size += INDICATOR_FACET_DELTA;
            let _ = build_test_facet(builder, indicator_size, Some(Color::red()));
            indicator_size += INDICATOR_FACET_DELTA;
            let _ = build_test_facet(builder, indicator_size, Some(Color::green()));
            indicator_size += INDICATOR_FACET_DELTA;
            let _ = build_test_facet(builder, indicator_size, Some(Color::blue()));
        });
        builder.group().stack().center().contents(|builder| {
            let _ = build_test_facet(builder, INNER_FACET_SIZE, Some(Color::white()));
            let _ = build_test_facet(builder, OUTER_FACET_SIZE, Some(Color::fuchsia()));
        });
    });
}

fn make_one_third_two_third(builder: &mut SceneBuilder) {
    builder.group().column().max_size().space_evenly().contents(|builder| {
        builder.group().row().max_size().space_evenly().contents(|builder| {
            let mut indicator_size = INDICATOR_FACET_SIZE;
            builder
                .group()
                .column_with_member_data(FlexMemberData::new(1))
                .cross_align(CrossAxisAlignment::End)
                .space_evenly()
                .contents(|builder| {
                    let _ = build_test_facet(builder, indicator_size, Some(Color::new()));
                });
            indicator_size += INDICATOR_FACET_DELTA;
            builder
                .group()
                .column_with_member_data(FlexMemberData::new(2))
                .space_evenly()
                .cross_align(CrossAxisAlignment::Start)
                .contents(|builder| {
                    let _ = build_test_facet(builder, indicator_size, Some(Color::red()));
                });
        });
    });
}

fn make_one_third_two_third_no_col(builder: &mut SceneBuilder) {
    builder.group().column().max_size().space_evenly().contents(|builder| {
        builder.group().row().max_size().space_evenly().contents(|builder| {
            let mut indicator_size = INDICATOR_FACET_SIZE;
            let _ = build_flexible_test_facet(
                builder,
                indicator_size,
                size2(f32::MAX, indicator_size.height),
                Some(Color::new()),
                FlexMemberData::new(1),
            );
            indicator_size += INDICATOR_FACET_DELTA;
            let _ = build_flexible_test_facet(
                builder,
                indicator_size,
                size2(f32::MAX, indicator_size.height),
                Some(Color::red()),
                FlexMemberData::new(2),
            );
        });
    });
}

fn make_text_alignment(
    builder: &mut SceneBuilder,
    font: &FontFace,
    size: Size,
    h: TextHorizontalAlignment,
    v: TextVerticalAlignment,
    visual_mode: usize,
) {
    const INSET: f32 = 50.0;
    let visual = (visual_mode & 0x1) != 0;
    let sized = (visual_mode & 0x2) != 0;
    let font_size = size.width.max(size.height) / 8.0;
    builder.group().stack().align(Alignment::center()).expand().contents(|builder| {
        let guide_color = Color::from_hash_code("#81b29a").expect("guide_color");
        builder.h_line(size.width, 3.0, guide_color, None);
        builder.v_line(size.height, 3.0, guide_color, None);
        builder.text_with_data(
            font.clone(),
            "Älign Me",
            font_size,
            Point::zero(),
            TextFacetOptions {
                color: Color::from_hash_code("#3d405b").expect("color"),
                background_color: Color::from_hash_code("#f2cc8f").ok(),
                horizontal_alignment: h,
                vertical_alignment: v,
                visual,
                ..TextFacetOptions::default()
            },
            if sized {
                StackMemberDataBuilder::new()
                    .top(INSET)
                    .left(INSET)
                    .bottom(INSET)
                    .right(INSET)
                    .build()
            } else {
                None
            },
        );
    });
}

#[cfg(test)]
mod test {
    use super::*;
    use carnelian::{
        scene::{layout::MainAxisSize, scene::SceneBuilder},
        Rect, Size,
    };
    use euclid::size2;
    use itertools::assert_equal;

    const TEST_LAYOUT_SIZE: Size = size2(1024.0, 600.00);

    fn stack_test(align: Alignment, expected_bounds: &[Rect]) {
        let mut builder = SceneBuilder::new();
        make_two_boxes(&mut builder, align);
        let mut scene = builder.build();
        scene.layout(TEST_LAYOUT_SIZE);
        let bounds = scene.all_facet_bounds();
        assert_equal(&bounds, expected_bounds);
    }

    fn stack_test_txt(align: Alignment, expected_text: &str) {
        let expected_bounds: BoundsHolder = toml::from_str(expected_text).unwrap();
        stack_test(align, &expected_bounds.bounds);
    }

    #[test]
    fn stack_two_boxes_top_left() {
        let expected_text = r#"[[bounds]]
                                origin = [0.0, 0.0]
                                size = [200.0, 50.0]

                                [[bounds]]
                                origin = [0.0, 0.0]
                                size = [300.0, 100.0]
                                "#;
        stack_test_txt(Alignment::top_left(), expected_text);
    }

    #[test]
    fn stack_two_boxes_top_center() {
        let expected_text = r#"
                                [[bounds]]
                                origin = [412.0, 0.0]
                                size = [200.0, 50.0]

                                [[bounds]]
                                origin = [362.0, 0.0]
                                size = [300.0, 100.0]
                                "#;
        stack_test_txt(Alignment::top_center(), expected_text);
    }

    #[test]
    fn stack_two_boxes_top_right() {
        let expected_text = r#"[[bounds]]
                            origin = [824.0, 0.0]
                            size = [200.0, 50.0]

                            [[bounds]]
                            origin = [724.0, 0.0]
                            size = [300.0, 100.0]
                            "#;
        stack_test_txt(Alignment::top_right(), expected_text);
    }

    #[test]
    fn stack_two_boxes_center_left() {
        let expected_text = r#"[[bounds]]
                            origin = [0.0, 275.0]
                            size = [200.0, 50.0]

                            [[bounds]]
                            origin = [0.0, 250.0]
                            size = [300.0, 100.0]
                            "#;
        stack_test_txt(Alignment::center_left(), expected_text);
    }

    #[test]
    fn stack_two_boxes_center() {
        let expected_text = r#"[[bounds]]
                            origin = [412.0, 275.0]
                            size = [200.0, 50.0]

                            [[bounds]]
                            origin = [362.0, 250.0]
                            size = [300.0, 100.0]
                            "#;
        stack_test_txt(Alignment::center(), expected_text);
    }

    #[test]
    fn stack_two_boxes_center_right() {
        let expected_text = r#"[[bounds]]
                            origin = [824.0, 275.0]
                            size = [200.0, 50.0]

                            [[bounds]]
                            origin = [724.0, 250.0]
                            size = [300.0, 100.0]
                            "#;
        stack_test_txt(Alignment::center_right(), expected_text);
    }

    #[test]
    fn stack_two_boxes_bottom_left() {
        let expected_text = r#"[[bounds]]
                            origin = [0.0, 550.0]
                            size = [200.0, 50.0]

                            [[bounds]]
                            origin = [0.0, 500.0]
                            size = [300.0, 100.0]
                            "#;
        stack_test_txt(Alignment::bottom_left(), expected_text);
    }

    #[test]
    fn stack_two_boxes_bottom_center() {
        let expected_text = r#"[[bounds]]
                            origin = [412.0, 550.0]
                            size = [200.0, 50.0]

                            [[bounds]]
                            origin = [362.0, 500.0]
                            size = [300.0, 100.0]
                            "#;
        stack_test_txt(Alignment::bottom_center(), expected_text);
    }

    #[test]
    fn stack_two_boxes_bottom_right() {
        let expected_text = r#"[[bounds]]
                            origin = [824.0, 550.0]
                            size = [200.0, 50.0]

                            [[bounds]]
                            origin = [724.0, 500.0]
                            size = [300.0, 100.0]
                            "#;
        stack_test_txt(Alignment::bottom_right(), expected_text);
    }

    const COLUMN_TEST_LAYOUT_SIZE: Size = size2(1024.0, 600.0);

    fn colummn_test(
        main_size: MainAxisSize,
        main_align: MainAxisAlignment,
        cross_align: CrossAxisAlignment,
        expected_bounds: &[Rect],
    ) {
        let mut builder = SceneBuilder::new();
        builder.group().stack().expand().align(Alignment::top_center()).contents(|builder| {
            make_column_with_two_boxes(builder, main_size, main_align, cross_align);
        });
        let mut scene = builder.build();
        scene.layout(COLUMN_TEST_LAYOUT_SIZE);
        let bounds = scene.all_facet_bounds();
        assert_equal(&bounds, expected_bounds);
    }

    fn colummn_test_txt(
        main_size: MainAxisSize,
        main_align: MainAxisAlignment,
        cross_align: CrossAxisAlignment,
        expected_text: &str,
    ) {
        let expected_bounds: BoundsHolder = toml::from_str(expected_text).unwrap();
        colummn_test(main_size, main_align, cross_align, &expected_bounds.bounds);
    }

    #[test]
    fn column_two_boxes_main_max() {
        let expected_text = r#"[[bounds]]
                            origin = [412.0, 0.0]
                            size = [200.0, 50.0]

                            [[bounds]]
                            origin = [362.0, 50.0]
                            size = [300.0, 100.0]
                            "#;
        colummn_test_txt(
            MainAxisSize::Max,
            MainAxisAlignment::Start,
            CrossAxisAlignment::Center,
            expected_text,
        );
    }

    #[test]
    fn column_two_boxes_main_min() {
        let expected_text = r#"[[bounds]]
                            origin = [412.0, 0.0]
                            size = [200.0, 50.0]

                            [[bounds]]
                            origin = [362.0, 50.0]
                            size = [300.0, 100.0]
                            "#;
        colummn_test_txt(
            MainAxisSize::Min,
            MainAxisAlignment::Start,
            CrossAxisAlignment::Center,
            expected_text,
        );
    }

    #[test]
    fn column_two_boxes_main_space_evenly() {
        let expected_text = r#"[[bounds]]
                            origin = [362.0, 150.0]
                            size = [200.0, 50.0]

                            [[bounds]]
                            origin = [362.0, 350.0]
                            size = [300.0, 100.0]
                            "#;
        colummn_test_txt(
            MainAxisSize::Max,
            MainAxisAlignment::SpaceEvenly,
            CrossAxisAlignment::Start,
            expected_text,
        );
    }

    #[test]
    fn column_two_boxes_main_center_center() {
        let expected_text = r#"[[bounds]]
                            origin = [412.0, 225.0]
                            size = [200.0, 50.0]

                            [[bounds]]
                            origin = [362.0, 275.0]
                            size = [300.0, 100.0]
                            "#;
        colummn_test_txt(
            MainAxisSize::Max,
            MainAxisAlignment::Center,
            CrossAxisAlignment::Center,
            expected_text,
        );
    }

    const BUTTON_TEST_LAYOUT_SIZE: Size = size2(1024.0, 600.0);

    #[test]
    fn fake_button() {
        let expected_text = r#"[[bounds]]
                            origin = [152.8, 158.33333]
                            size = [80.0, 40.0]

                            [[bounds]]
                            origin = [385.6, 153.33333]
                            size = [70.0, 50.0]

                            [[bounds]]
                            origin = [608.4, 148.33333]
                            size = [60.0, 60.0]

                            [[bounds]]
                            origin = [821.2, 143.33333]
                            size = [50.0, 70.0]

                            [[bounds]]
                            origin = [412.0, 381.66666]
                            size = [200.0, 50.0]

                            [[bounds]]
                            origin = [362.0, 356.66666]
                            size = [300.0, 100.0]
        "#;
        let expected_bounds: BoundsHolder = toml::from_str(expected_text).unwrap();
        let mut builder = SceneBuilder::new();
        builder.group().stack().expand().align(Alignment::top_center()).contents(|builder| {
            make_fake_button(builder);
        });
        let mut scene = builder.build();
        scene.layout(BUTTON_TEST_LAYOUT_SIZE);
        let bounds = scene.all_facet_bounds();
        assert_equal(bounds, expected_bounds.bounds);
    }

    const ONE_THIRD_TWO_THIRDS_TEST_LAYOUT_SIZE: Size = size2(1024.0, 600.0);

    #[test]
    fn one_third_two_thirds() {
        let expected_text = r#"[[bounds]]
        origin = [291.33334, 280.0]
        size = [80.0, 40.0]

        [[bounds]]
        origin = [371.33334, 275.0]
        size = [70.0, 50.0]
        "#;
        let expected_bounds: BoundsHolder = toml::from_str(expected_text).unwrap();
        let mut builder = SceneBuilder::new();
        builder.group().stack().expand().align(Alignment::top_center()).contents(|builder| {
            make_one_third_two_third(builder);
        });
        let mut scene = builder.build();
        scene.layout(ONE_THIRD_TWO_THIRDS_TEST_LAYOUT_SIZE);
        let bounds = scene.all_facet_bounds();
        assert_equal(bounds, expected_bounds.bounds);
    }
}
