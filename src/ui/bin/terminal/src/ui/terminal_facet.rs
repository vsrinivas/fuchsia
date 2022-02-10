// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    carnelian::{
        color::Color,
        drawing::path_for_rounded_rectangle,
        render::{BlendMode, Context as RenderContext, Fill, FillRule, Layer, Raster, Style},
        scene::{facets::Facet, LayerGroup, SceneOrder},
        Coord, Rect, Size, ViewAssistantContext,
    },
    fuchsia_trace as ftrace,
    std::{any::Any, cell::RefCell, convert::TryFrom, rc::Rc},
    term_model::{ansi::TermInfo, config::Config, Term},
    terminal::{renderable_layers, FontSet, Offset, Renderer},
};

/// Empty type for term model config
pub struct UIConfig;

impl Default for UIConfig {
    fn default() -> UIConfig {
        UIConfig
    }
}

pub type TerminalConfig = Config<UIConfig>;

pub enum TerminalMessages {
    #[allow(dead_code)]
    SetScrollThumbMessage(Option<(Rect, f32)>),
}

fn raster_for_rounded_rectangle(
    bounds: &Rect,
    corner_radius: Coord,
    render_context: &mut RenderContext,
) -> Raster {
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path_for_rounded_rectangle(bounds, corner_radius, render_context), None);
    raster_builder.build()
}

/// Facet that implements a terminal text grid with a scroll bar.
pub struct TerminalFacet<T> {
    font_set: FontSet,
    size: Size,
    term: Rc<RefCell<Term<T>>>,
    scroll_thumb: Option<(Rect, f32)>,
    thumb_order: Option<SceneOrder>,
    renderer: Renderer,
}

impl<T: 'static> TerminalFacet<T> {
    pub fn new(font_set: FontSet, cell_size: &Size, term: Rc<RefCell<Term<T>>>) -> Self {
        let renderer = Renderer::new(&font_set, cell_size);

        TerminalFacet {
            font_set,
            size: Size::zero(),
            term,
            scroll_thumb: None,
            thumb_order: None,
            renderer,
        }
    }
}

impl<T: 'static> Facet for TerminalFacet<T> {
    fn update_layers(
        &mut self,
        _: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        ftrace::duration!("terminal", "TerminalFacet:update_layers");

        self.size = view_context.size;

        let config = TerminalConfig::default();
        let term = self.term.borrow();
        let cols = term.cols().0;
        let rows = term.lines().0;
        let stride = cols * 4;
        let new_thumb_order =
            SceneOrder::try_from(stride * rows).unwrap_or_else(|e| panic!("{}", e));

        // Remove old scrollbar thumb.
        if let Some(thumb_order) = self.thumb_order.take() {
            layer_group.remove(thumb_order);
        }

        let offset = Offset { column: 0, row: 0 };
        let layers = renderable_layers(&term, &config, &offset);
        self.renderer.render(layer_group, render_context, &self.font_set, layers);

        // Add new scrollbar thumb.
        if let Some((thumb, alpha)) = self.scroll_thumb {
            // Linear to sRGB.
            let srgb = (alpha.powf(1.0 / 2.2) * 255.0) as u8;
            let color = Color { r: srgb, g: srgb, b: srgb, a: (alpha * 255.0) as u8 };
            let layer = Layer {
                raster: raster_for_rounded_rectangle(&thumb, 2.0, render_context),
                clip: None,
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(color),
                    blend_mode: BlendMode::Over,
                },
            };
            layer_group.insert(new_thumb_order, layer);
            self.thumb_order = Some(new_thumb_order);
        }

        Ok(())
    }

    fn handle_message(&mut self, message: Box<dyn Any>) {
        if let Some(message) = message.downcast_ref::<TerminalMessages>() {
            match message {
                TerminalMessages::SetScrollThumbMessage(thumb) => {
                    self.scroll_thumb = *thumb;
                }
            }
        }
    }

    fn calculate_size(&self, _: Size) -> Size {
        self.size
    }
}
