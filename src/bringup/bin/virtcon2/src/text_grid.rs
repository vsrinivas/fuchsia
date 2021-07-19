// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::colors::ColorScheme,
    carnelian::{
        color::Color,
        drawing::{FontFace, GlyphMap, TextGrid, TextGridCell},
        render::{BlendMode, Context as RenderContext, Fill, FillRule, Layer, Path, Raster, Style},
        scene::{facets::Facet, LayerGroup},
        Size, ViewAssistantContext,
    },
    euclid::point2,
    std::{any::Any, cell::RefCell, collections::HashMap, convert::TryFrom, rc::Rc},
    term_model::{
        ansi::CursorStyle,
        term::{color::Rgb, RenderableCellContent},
        Term,
    },
};

pub fn font_to_cell_size(font_size: f32, cell_padding: f32) -> Size {
    let height = font_size + cell_padding;
    let width = height / 2.0;

    Size::new(width, height)
}

enum CellContent {
    Cursor(CursorStyle),
    Char(char),
}

// The term-model library gives us zero-width characters in our array of chars. However,
// we do not support this at this point so we just pull out the first char for rendering.
impl From<RenderableCellContent> for CellContent {
    fn from(content: RenderableCellContent) -> Self {
        match content {
            RenderableCellContent::Cursor(cursor_key) => Self::Cursor(cursor_key.style),
            RenderableCellContent::Chars(chars) => Self::Char(chars[0]),
        }
    }
}

// Thickness of cursor lines is determined by multiplying this with the cell height.
const CURSOR_LINE_THICKNESS_FACTOR: f32 = 1.0 / 15.0;

fn path_for_block(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    path_builder
        .move_to(point2(0.0, 0.0))
        .line_to(point2(size.width, 0.0))
        .line_to(point2(size.width, size.height))
        .line_to(point2(0.0, size.height))
        .line_to(point2(0.0, 0.0));
    path_builder.build()
}

fn path_for_underline(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let top = size.height - size.height * CURSOR_LINE_THICKNESS_FACTOR;
    path_builder
        .move_to(point2(0.0, top))
        .line_to(point2(size.width, top))
        .line_to(point2(size.width, size.height))
        .line_to(point2(0.0, size.height))
        .line_to(point2(0.0, top));
    path_builder.build()
}

fn path_for_beam(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let right = size.height * CURSOR_LINE_THICKNESS_FACTOR;
    path_builder
        .move_to(point2(0.0, 0.0))
        .line_to(point2(right, 0.0))
        .line_to(point2(right, size.height))
        .line_to(point2(0.0, size.height))
        .line_to(point2(0.0, 0.0));
    path_builder.build()
}

fn path_for_hollow_block(size: &Size, render_context: &mut RenderContext) -> Path {
    let mut path_builder = render_context.path_builder().expect("path_builder");
    let inset = size.height * CURSOR_LINE_THICKNESS_FACTOR;
    let bottom_start = size.height - inset;
    let right_start = size.width - inset;
    path_builder
        // top
        .move_to(point2(0.0, 0.0))
        .line_to(point2(size.width, 0.0))
        .line_to(point2(size.width, inset))
        .line_to(point2(0.0, inset))
        .line_to(point2(0.0, 0.0))
        // bottom
        .move_to(point2(0.0, bottom_start))
        .line_to(point2(size.width, bottom_start))
        .line_to(point2(size.width, size.height))
        .line_to(point2(0.0, size.height))
        .line_to(point2(0.0, bottom_start))
        // left
        .move_to(point2(0.0, inset))
        .line_to(point2(inset, inset))
        .line_to(point2(inset, bottom_start))
        .line_to(point2(0.0, bottom_start))
        .line_to(point2(0.0, inset))
        // right
        .move_to(point2(right_start, inset))
        .line_to(point2(size.width, inset))
        .line_to(point2(size.width, bottom_start))
        .line_to(point2(right_start, bottom_start))
        .line_to(point2(right_start, inset));
    path_builder.build()
}

fn maybe_path_for_cursor_style(
    render_context: &mut RenderContext,
    cursor_style: CursorStyle,
    cell_size: &Size,
) -> Option<Path> {
    match cursor_style {
        CursorStyle::Block => Some(path_for_block(cell_size, render_context)),
        CursorStyle::Underline => Some(path_for_underline(cell_size, render_context)),
        CursorStyle::Beam => Some(path_for_beam(cell_size, render_context)),
        CursorStyle::HollowBlock => Some(path_for_hollow_block(cell_size, render_context)),
        CursorStyle::Hidden => None,
    }
}

fn maybe_raster_for_cursor_style(
    render_context: &mut RenderContext,
    cursor_style: CursorStyle,
    column: usize,
    row: usize,
    cell_size: &Size,
    cursors: &mut HashMap<CursorStyle, Option<Raster>>,
) -> Option<Raster> {
    cursors
        .entry(cursor_style)
        .or_insert_with(|| {
            maybe_path_for_cursor_style(render_context, cursor_style, cell_size).as_ref().map(|p| {
                let mut raster_builder = render_context.raster_builder().expect("raster_builder");
                raster_builder.add(p, None);
                raster_builder.build()
            })
        })
        .as_ref()
        .map(|r| {
            let cell_position =
                point2(cell_size.width * column as f32, cell_size.height * row as f32);
            r.clone().translate(cell_position.to_vector().to_i32())
        })
}

fn maybe_raster_for_cell_content(
    render_context: &mut RenderContext,
    content: &CellContent,
    column: usize,
    row: usize,
    cell_size: &Size,
    textgrid: &TextGrid,
    font: &FontFace,
    glyphs: &mut GlyphMap,
    cursors: &mut HashMap<CursorStyle, Option<Raster>>,
) -> Option<Raster> {
    match content {
        CellContent::Cursor(cursor_style) => maybe_raster_for_cursor_style(
            render_context,
            *cursor_style,
            column,
            row,
            cell_size,
            cursors,
        ),
        CellContent::Char(c) => {
            let grid_cell =
                TextGridCell::new(render_context, column, row, *c, textgrid, font, glyphs);
            grid_cell.raster
        }
    }
}

fn make_color(term_color: &Rgb) -> Color {
    Color { r: term_color.r, g: term_color.g, b: term_color.b, a: 0xff }
}

/// Facet that implements a virtcon-style text grid with a status bar
/// and terminal output.
pub struct TextGridFacet<T> {
    cell_size: Size,
    textgrid: TextGrid,
    font: FontFace,
    glyphs: GlyphMap,
    cursors: HashMap<CursorStyle, Option<Raster>>,
    color_scheme: ColorScheme,
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
        color_scheme: ColorScheme,
        term: Option<Rc<RefCell<Term<T>>>>,
        status: Vec<(String, Color)>,
        status_tab_width: usize,
        cell_padding: f32,
    ) -> Self {
        let cell_size = font_to_cell_size(font_size, cell_padding);
        let textgrid = TextGrid::new(cell_size, cell_padding);
        let glyphs = GlyphMap::new();
        let cursors = HashMap::new();

        Self {
            cell_size,
            textgrid,
            font,
            glyphs,
            cursors,
            color_scheme,
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

        let config = self.color_scheme.into();
        let term = self.term.as_ref().map(|t| t.borrow());
        let status_tab_width = &self.status_tab_width;
        let background = self.color_scheme.back;

        // Create an iterator over cells used for the status bar followed by active
        // terminal cells. Each cell has column, row and foreground/background colors.
        let cells = self
            .status
            .iter()
            .enumerate()
            .flat_map(|(i, (s, color))| {
                let start = i * status_tab_width;
                s.chars()
                    .enumerate()
                    .map(move |(x, c)| (start + x, 0, CellContent::Char(c), *color, background))
            })
            .chain(term.iter().flat_map(|term| {
                term.renderable_cells(&config).map(|cell| {
                    (
                        cell.column.0,
                        cell.line.0 + 1,
                        cell.inner.into(),
                        make_color(&cell.fg),
                        make_color(&cell.bg),
                    )
                })
            }));

        let glyphs = &mut self.glyphs;
        let cursors = &mut self.cursors;
        let font = &self.font;
        let textgrid = &self.textgrid;
        let cell_size = &self.cell_size;

        // Create an iterator over all layers.
        let layers = cells
            .flat_map(|(column, row, content, fg, bg)| {
                // Yield a background raster if needed.
                if bg != background {
                    // Use a block cursor for background.
                    maybe_raster_for_cursor_style(
                        render_context,
                        CursorStyle::Block,
                        column,
                        row,
                        cell_size,
                        cursors,
                    )
                } else {
                    None
                }
                .map(|r| (r, bg))
                .into_iter()
                .chain(
                    maybe_raster_for_cell_content(
                        render_context,
                        &content,
                        column,
                        row,
                        cell_size,
                        textgrid,
                        font,
                        glyphs,
                        cursors,
                    )
                    .map(|r| (r, fg))
                    .into_iter(),
                )
            })
            .map(|(raster, color)| Layer {
                raster,
                clip: None,
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(color),
                    blend_mode: BlendMode::Over,
                },
            });

        layer_group.clear();
        for (i, layer) in layers.enumerate() {
            layer_group.insert(u16::try_from(i).expect("too many layers"), layer);
        }

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
        let _ = TextGridFacet::<TestListener>::new(
            font,
            14.0,
            ColorScheme::default(),
            None,
            vec![],
            24,
            2.0,
        );
        Ok(())
    }
}
