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
        layout::{Alignment, CrossAxisAlignment, MainAxisAlignment, MainAxisSize},
        scene::{Scene, SceneBuilder},
        LayerGroup,
    },
    App, AppAssistant, Point, Rect, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr,
    ViewKey,
};
use euclid::size2;
use fuchsia_zircon::Event;
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

struct SceneDetails {
    scene: Scene,
}

enum Mode {
    Stack(usize),
    Flex(usize, usize, usize),
    Button,
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
}

#[derive(Serialize, Deserialize)]
struct BoundsHolder {
    bounds: Vec<Rect>,
}

impl LayoutsViewAssistant {
    fn new() -> Result<ViewAssistantPtr, Error> {
        let face = load_font(PathBuf::from("/pkg/data/fonts/RobotoSlab-Regular.ttf"))?;
        Ok(Box::new(LayoutsViewAssistant { face, mode: Mode::default(), scene_details: None }))
    }

    fn cycle1(&mut self) {
        match &mut self.mode {
            Mode::Stack(alignment) => {
                *alignment = (*alignment + 1) % ALIGNS.len();
            }
            Mode::Flex(main_size, ..) => {
                *main_size = (*main_size + 1) % MAIN_SIZES.len();
            }
            _ => (),
        }
        self.scene_details = None;
    }

    fn cycle2(&mut self) {
        match &mut self.mode {
            Mode::Flex(_, main_align, ..) => {
                *main_align = (*main_align + 1) % MAIN_ALIGNS.len();
            }
            _ => (),
        }
        self.scene_details = None;
    }

    fn cycle3(&mut self) {
        match &mut self.mode {
            Mode::Flex(_, _, cross_align) => {
                *cross_align = (*cross_align + 1) % CROSS_ALIGNS.len();
            }
            _ => (),
        }
        self.scene_details = None;
    }

    fn cycle_mode(&mut self) {
        self.mode = match self.mode {
            Mode::Stack(..) => Mode::Flex(0, 0, 0),
            Mode::Flex(..) => Mode::Button,
            Mode::Button => Mode::Stack(0),
        };
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
}

impl ViewAssistant for LayoutsViewAssistant {
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
            let mut builder = SceneBuilder::new().background_color(Color::white());
            builder.group().stack().expand().align(Alignment::top_center()).contents(|builder| {
                let label = match self.mode {
                    Mode::Stack(alignment) => format!("make_two_boxes_{:?}", ALIGNS[alignment]()),
                    Mode::Flex(main_size, main_align, cross_align) => format!(
                        "make_column_with_two_boxes_{:?}_{:?}_{:?}",
                        MAIN_SIZES[main_size], MAIN_ALIGNS[main_align], CROSS_ALIGNS[cross_align],
                    ),
                    Mode::Button => String::from("button"),
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
                }
            });
            let mut scene = builder.build();
            scene.layout(context.size);
            SceneDetails { scene }
        });

        scene_details.scene.render(render_context, ready_event, context)?;
        self.scene_details = Some(scene_details);
        context.request_render();
        Ok(())
    }

    fn handle_keyboard_event(
        &mut self,
        _context: &mut ViewAssistantContext,
        _event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        const M: u32 = 109;
        const ONE: u32 = 49;
        const TWO: u32 = 50;
        const THREE: u32 = 51;
        const D: u32 = 100;
        if let Some(code_point) = keyboard_event.code_point {
            if keyboard_event.phase == input::keyboard::Phase::Pressed {
                match code_point {
                    ONE => self.cycle1(),
                    TWO => self.cycle2(),
                    THREE => self.cycle3(),
                    M => self.cycle_mode(),
                    D => self.dump_bounds(),
                    _ => println!("code_point = {}", code_point),
                }
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
    let e: u8 = rng.gen_range(0, 128);
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

struct TestFacet {
    size: Size,
    color: Color,
    raster: Option<Raster>,
}

impl TestFacet {
    fn new(size: Size, color: Option<Color>) -> Self {
        Self { size, color: color.unwrap_or_else(|| random_color()), raster: None }
    }
}

impl Facet for TestFacet {
    fn update_layers(
        &mut self,
        _size: Size,
        layer_group: &mut LayerGroup,
        render_context: &mut RenderContext,
        _view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let line_raster = self.raster.take().unwrap_or_else(|| {
            let line_path = path_for_rectangle(&Rect::from_size(self.size), render_context);
            let mut raster_builder = render_context.raster_builder().expect("raster_builder");
            raster_builder.add(&line_path, None);
            raster_builder.build()
        });
        let raster = line_raster.clone();
        self.raster = Some(line_raster);
        layer_group.replace_all(std::iter::once(Layer {
            raster: raster,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(self.color),
                blend_mode: BlendMode::Over,
            },
        }));
        Ok(())
    }

    fn get_size(&self) -> Size {
        self.size
    }
}

fn build_test_facet(builder: &mut SceneBuilder, size: Size, color: Option<Color>) -> FacetId {
    let facet = TestFacet::new(size, color);
    builder.facet(Box::new(facet))
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
}
