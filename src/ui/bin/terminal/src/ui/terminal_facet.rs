// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    carnelian::{
        color::Color,
        drawing::{path_for_rectangle, FontFace},
        render::{
            BlendMode, Context as RenderContext, Fill, FillRule, Layer, Order, Raster, Style,
        },
        scene::{facets::Facet, LayerGroup},
        Rect, Size, ViewAssistantContext,
    },
    fuchsia_trace as ftrace,
    std::{any::Any, cell::RefCell, convert::TryFrom, rc::Rc},
    term_model::{ansi::TermInfo, config::Config, Term},
    terminal::{renderable_layers, Offset, Renderer},
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
    SetScrollThumbMessage(Option<Rect>),
}

fn raster_for_rectangle(bounds: &Rect, render_context: &mut RenderContext) -> Raster {
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path_for_rectangle(bounds, render_context), None);
    raster_builder.build()
}

/// Facet that implements a terminal text grid with a scroll bar.
pub struct TerminalFacet<T> {
    font: FontFace,
    size: Size,
    term: Rc<RefCell<Term<T>>>,
    scroll_thumb: Option<Rect>,
    thumb_order: Option<Order>,
    renderer: Renderer,
}

impl<T: 'static> TerminalFacet<T> {
    pub fn new(
        font: FontFace,
        font_size: f32,
        term: Rc<RefCell<Term<T>>>,
        cell_padding: f32,
        scroll_thumb: Option<Rect>,
    ) -> Self {
        let renderer = Renderer::new(font_size, cell_padding);

        TerminalFacet { font, size: Size::zero(), term, scroll_thumb, thumb_order: None, renderer }
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
        let new_thumb_order = Order::try_from(stride * rows).unwrap_or_else(|e| panic!("{}", e));

        // Remove old scrollbar thumb.
        if let Some(thumb_order) = self.thumb_order.take() {
            layer_group.remove(thumb_order);
        }

        let offset = Offset { column: 0, row: 0 };
        let layers = renderable_layers(&term, &config, &offset);
        self.renderer.render(layer_group, render_context, &self.font, layers);

        // Add new scrollbar thumb.
        if let Some(thumb) = self.scroll_thumb {
            let layer = Layer {
                raster: raster_for_rectangle(&thumb, render_context),
                clip: None,
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(Color::white()),
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
