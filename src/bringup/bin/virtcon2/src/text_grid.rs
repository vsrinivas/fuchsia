// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::terminal::TerminalConfig,
    carnelian::{
        color::Color,
        drawing::{FontFace, GlyphMap, TextGrid, TextGridCell},
        render::{BlendMode, Context as RenderContext, Fill, FillRule, Layer, Style},
        scene::{facets::Facet, LayerGroup},
        Size, ViewAssistantContext,
    },
    std::{any::Any, cell::RefCell, rc::Rc},
    term_model::{
        ansi::CursorStyle,
        term::{CursorKey, RenderableCellContent},
        Term,
    },
};

pub fn font_to_cell_size(font_size: f32, cell_padding: f32) -> Size {
    let height = font_size + cell_padding;
    let width = height / 2.0;

    Size::new(width, height)
}

// The term-model library gives us zero-width characters in our array of chars. However,
// we do not support this at this point so we just pull out the first char for rendering.
fn maybe_char_for_renderable_cell_content(content: RenderableCellContent) -> Option<char> {
    match content {
        RenderableCellContent::Cursor(cursor_key) => chars_for_cursor(cursor_key),
        RenderableCellContent::Chars(chars) => Some(chars[0]),
    }
}

// TODO: Implement rasters for non-font based cursors.
const BOX_CURSOR_CHAR: char = '_';
const UNDERLINE_CURSOR_CHAR: char = '_';
const BEAM_CURSOR_CHAR: char = '|';
const HOLLOW_BLOCK_CURSOR_CHAR: char = '_';

fn chars_for_cursor(cursor: CursorKey) -> Option<char> {
    match cursor.style {
        CursorStyle::Block => Some(BOX_CURSOR_CHAR),
        CursorStyle::Underline => Some(UNDERLINE_CURSOR_CHAR),
        CursorStyle::Beam => Some(BEAM_CURSOR_CHAR),
        CursorStyle::HollowBlock => Some(HOLLOW_BLOCK_CURSOR_CHAR),
        CursorStyle::Hidden => None,
    }
}

/// Facet that implements a virtcon-style text grid with a status bar
/// and terminal output.
pub struct TextGridFacet<T> {
    textgrid: TextGrid,
    font: FontFace,
    glyphs: GlyphMap,
    foreground: Color,
    size: Size,
    term: Option<Rc<RefCell<Term<T>>>>,
    status: Vec<(String, Color)>,
    status_tab_width: usize,
}

pub enum TextGridMessages<T> {
    SetTermMessage(Rc<RefCell<Term<T>>>),
    ChangeStatusMessage(Vec<(String, Color)>),
}

impl<T> TextGridFacet<T> {
    pub fn new(
        font: FontFace,
        font_size: f32,
        foreground: Color,
        term: Option<Rc<RefCell<Term<T>>>>,
        status: Vec<(String, Color)>,
        status_tab_width: usize,
        cell_padding: f32,
    ) -> Self {
        let cell_size = font_to_cell_size(font_size, cell_padding);
        let textgrid = TextGrid::new(cell_size, cell_padding);
        let glyphs = GlyphMap::new();

        Self {
            textgrid,
            font,
            glyphs,
            foreground,
            size: Size::zero(),
            term,
            status,
            status_tab_width,
        }
    }
}

impl<T: 'static> Facet for TextGridFacet<T> {
    fn update_layers(
        &mut self,
        size: Size,
        layer_group: &mut LayerGroup,
        render_context: &mut RenderContext,
        _: &ViewAssistantContext,
    ) -> std::result::Result<(), anyhow::Error> {
        self.size = size;

        let config = TerminalConfig::default();
        let term = self.term.as_ref().map(|t| t.borrow());
        let foreground = &self.foreground;
        let status_tab_width = &self.status_tab_width;

        // Create an iterator over cells used for the status bar followed by active
        // terminal cells. Each cell has column, row and foreground color.
        let cells = self
            .status
            .iter()
            .enumerate()
            .flat_map(|(i, (s, color))| {
                let start = i * status_tab_width;
                s.chars().enumerate().map(move |(x, c)| (start + x, 0, c, color))
            })
            .chain(term.iter().flat_map(|term| {
                term.renderable_cells(&config).filter_map(|cell| {
                    if let Some(c) = maybe_char_for_renderable_cell_content(cell.inner) {
                        Some((cell.column.0, cell.line.0 + 1, c, foreground))
                    } else {
                        None
                    }
                })
            }));

        let glyphs = &mut self.glyphs;
        let font = &self.font;
        let textgrid = &self.textgrid;

        // Create an iterator over all layers.
        let layers = cells
            .filter_map(|(column, row, c, foreground)| {
                let grid_cell =
                    TextGridCell::new(render_context, column, row, c, textgrid, font, glyphs);
                grid_cell.raster.map(|r| (r, foreground))
            })
            .map(|(raster, color)| Layer {
                raster,
                clip: None,
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(*color),
                    blend_mode: BlendMode::Over,
                },
            });

        layer_group.replace_all(layers);

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

    fn get_size(&self) -> Size {
        self.size
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
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
        let _ =
            TextGridFacet::<TestListener>::new(font, 14.0, Color::white(), None, vec![], 24, 2.0);
        Ok(())
    }
}
