// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::colors::ColorScheme,
    crate::terminal::TerminalConfig,
    carnelian::{
        color::Color,
        render::Context as RenderContext,
        scene::{facets::Facet, LayerGroup},
        Size, ViewAssistantContext,
    },
    fuchsia_trace::duration,
    std::{any::Any, cell::RefCell, rc::Rc},
    term_model::{
        ansi::{CursorStyle, TermInfo},
        term::{cell::Flags, color::Rgb},
        Term,
    },
    terminal::{renderable_layers, FontSet, LayerContent, Offset, RenderableLayer, Renderer},
};

fn make_rgb(color: &Color) -> Rgb {
    Rgb { r: color.r, g: color.g, b: color.b }
}

/// Facet that implements a virtcon-style text grid with a status bar
/// and terminal output.
pub struct TextGridFacet<T> {
    font_set: FontSet,
    color_scheme: ColorScheme,
    size: Size,
    term: Option<Rc<RefCell<Term<T>>>>,
    status: Vec<(String, Rgb)>,
    status_tab_width: usize,
    renderer: Renderer,
}

pub enum TextGridMessages<T> {
    SetTermMessage(Rc<RefCell<Term<T>>>),
    ChangeStatusMessage(Vec<(String, Rgb)>),
}

const STATUS_BG: Rgb = Rgb { r: 0, g: 0, b: 0 };

impl<T> TextGridFacet<T> {
    pub fn new(
        font_set: FontSet,
        cell_size: &Size,
        color_scheme: ColorScheme,
        term: Option<Rc<RefCell<Term<T>>>>,
        status: Vec<(String, Rgb)>,
        status_tab_width: usize,
    ) -> Self {
        let renderer = Renderer::new(&font_set, cell_size);

        Self {
            font_set,
            color_scheme,
            size: Size::zero(),
            term,
            status,
            status_tab_width,
            renderer,
        }
    }
}

impl<T: 'static> Facet for TextGridFacet<T> {
    fn update_layers(
        &mut self,
        _: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        view_context: &ViewAssistantContext,
    ) -> std::result::Result<(), anyhow::Error> {
        duration!("gfx", "TextGrid::update_layers");

        self.size = view_context.size;

        let config: TerminalConfig = self.color_scheme.into();
        let term = self.term.as_ref().map(|t| t.borrow());
        let status_tab_width = self.status_tab_width;
        let columns = term.as_ref().map(|t| t.cols().0).unwrap_or(1);
        let bg = make_rgb(&self.color_scheme.back);
        // First row is used for the status bar.
        let term_offset = Offset { column: 0, row: 1 };

        // Create an iterator over cells used for the status bar followed by active
        // terminal cells. Each layer has an order, contents, and color.
        //
        // The order of layers will be stable unless the number of columns change.
        //
        // Status bar is a set of background layers, followed by foreground layers.
        let layers = if STATUS_BG != bg {
            Some((0..columns).into_iter().map(|x| RenderableLayer {
                order: x,
                column: x,
                row: 0,
                content: LayerContent::Cursor(CursorStyle::Block),
                rgb: STATUS_BG,
            }))
        } else {
            None
        }
        .into_iter()
        .flat_map(|iter| iter)
        .chain(self.status.iter().enumerate().flat_map(|(i, (s, rgb))| {
            let start = i * status_tab_width;
            let order = columns + start;
            s.chars().enumerate().map(move |(x, c)| RenderableLayer {
                order: order + x,
                column: start + x,
                row: 0,
                content: LayerContent::Char((c, Flags::empty())),
                rgb: *rgb,
            })
        }))
        .chain(term.iter().flat_map(|term| renderable_layers(term, &config, &term_offset)));

        self.renderer.render(layer_group, render_context, &self.font_set, layers);

        Ok(())
    }

    fn handle_message(&mut self, message: Box<dyn Any>) {
        if let Some(message) = message.downcast_ref::<TextGridMessages<T>>() {
            match message {
                TextGridMessages::SetTermMessage(term) => {
                    self.term = Some(Rc::clone(term));
                }
                TextGridMessages::ChangeStatusMessage(status) => {
                    self.status = status.clone();
                }
            }
        }
    }

    fn calculate_size(&self, _: Size) -> Size {
        self.size
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::colors::ColorScheme,
        anyhow::Error,
        carnelian::drawing::load_font,
        std::path::PathBuf,
        term_model::event::{Event, EventListener},
    };

    #[derive(Default)]
    struct TestListener;

    impl EventListener for TestListener {
        fn send_event(&self, _event: Event) {}
    }

    const FONT: &'static str = "/pkg/data/font.ttf";

    #[test]
    fn can_create_text_grid() -> Result<(), Error> {
        let font = load_font(PathBuf::from(FONT))?;
        let font_set = FontSet::new(font, None, None, None, vec![]);
        let _ = TextGridFacet::<TestListener>::new(
            font_set,
            &Size::new(8.0, 16.0),
            ColorScheme::default(),
            None,
            vec![],
            24,
        );
        Ok(())
    }
}
