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
    fuchsia_trace::duration,
    rustc_hash::{FxHashMap, FxHashSet},
    std::{any::Any, cell::RefCell, collections::hash_map::Entry, convert::TryFrom, mem, rc::Rc},
    term_model::{
        ansi::{CursorStyle, TermInfo},
        term::{color::Rgb, RenderableCellContent},
        Term,
    },
};

pub fn font_to_cell_size(font_size: f32, cell_padding: f32) -> Size {
    let height = font_size + cell_padding;
    let width = height / 2.0;

    // Round to the smallest size equal or greater.
    Size::new(width, height).ceil()
}

#[derive(PartialEq)]
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

#[derive(PartialEq)]
struct CellId {
    content: CellContent,
    rgb: Rgb,
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
    cursors: &mut FxHashMap<CursorStyle, Option<Raster>>,
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
            let raster = r.clone().translate(cell_position.to_vector().to_i32());
            // Add empty raster to enable caching of the translated cursor.
            // TODO: add more appropriate API for this.
            let empty_raster = {
                let raster_builder = render_context.raster_builder().unwrap();
                raster_builder.build()
            };
            raster + empty_raster
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
    cursors: &mut FxHashMap<CursorStyle, Option<Raster>>,
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

fn make_rgb(color: &Color) -> Rgb {
    Rgb { r: color.r, g: color.g, b: color.b }
}

/// Facet that implements a virtcon-style text grid with a status bar
/// and terminal output.
pub struct TextGridFacet<T> {
    cell_size: Size,
    textgrid: TextGrid,
    font: FontFace,
    glyphs: GlyphMap,
    cursors: FxHashMap<CursorStyle, Option<Raster>>,
    color_scheme: ColorScheme,
    size: Size,
    term: Option<Rc<RefCell<Term<T>>>>,
    status: Vec<(String, Rgb)>,
    status_tab_width: usize,
    cells: FxHashMap<u16, CellId>,
    old_cells: FxHashSet<u16>,
    new_cells: FxHashSet<u16>,
}

pub enum TextGridMessages<T> {
    SetTermMessage(Rc<RefCell<Term<T>>>),
    ChangeStatusMessage(Vec<(String, Rgb)>),
}

const STATUS_BG: Rgb = Rgb { r: 0, g: 0, b: 0 };

impl<T> TextGridFacet<T> {
    pub fn new(
        font: FontFace,
        font_size: f32,
        color_scheme: ColorScheme,
        term: Option<Rc<RefCell<Term<T>>>>,
        status: Vec<(String, Rgb)>,
        status_tab_width: usize,
        cell_padding: f32,
    ) -> Self {
        let cell_size = font_to_cell_size(font_size, cell_padding);
        let textgrid = TextGrid::new(cell_size, cell_padding);
        let glyphs = GlyphMap::new();
        let cursors = FxHashMap::default();
        let cells = FxHashMap::default();
        let old_cells = FxHashSet::default();
        let new_cells = FxHashSet::default();

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
            cells,
            old_cells,
            new_cells,
        }
    }
}

impl<T: 'static> Facet for TextGridFacet<T> {
    fn update_layers(
        &mut self,
        size: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        _: &ViewAssistantContext,
    ) -> std::result::Result<(), anyhow::Error> {
        duration!("gfx", "TextGrid::update_layers");

        self.size = size;

        let config = self.color_scheme.into();
        let term = self.term.as_ref().map(|t| t.borrow());
        let status_tab_width = self.status_tab_width;
        let columns = term.as_ref().map(|t| t.cols().0).unwrap_or(1);
        let bg = make_rgb(&self.color_scheme.back);

        // Create an iterator over cells used for the status bar followed by active
        // terminal cells. Each layer has an order, contents, and color.
        //
        // The order of layers will be stable unless the number of columns change. Each
        // row of background layers is followed by one row of foreground layers.
        let cells = (0..columns)
            .into_iter()
            .map(|x| {
                (
                    x,
                    x,
                    0,
                    if STATUS_BG != bg {
                        CellContent::Cursor(CursorStyle::Block)
                    } else {
                        CellContent::Cursor(CursorStyle::Hidden)
                    },
                    STATUS_BG,
                )
            })
            .chain(self.status.iter().enumerate().flat_map(|(i, (s, rgb))| {
                let start = i * status_tab_width;
                let order = columns + start;
                s.chars()
                    .enumerate()
                    .map(move |(x, c)| (order + x, start + x, 0, CellContent::Char(c), *rgb))
            }))
            .chain(term.iter().flat_map(|term| {
                let stride = columns * 2;
                term.renderable_cells(&config).flat_map(move |cell| {
                    let row = cell.line.0 + 1;
                    let order = row * stride + cell.column.0;
                    std::iter::once((
                        order,
                        cell.column.0,
                        row,
                        if cell.bg != bg {
                            CellContent::Cursor(CursorStyle::Block)
                        } else {
                            CellContent::Cursor(CursorStyle::Hidden)
                        },
                        cell.bg,
                    ))
                    .chain(std::iter::once((
                        order + columns,
                        cell.column.0,
                        row,
                        cell.inner.into(),
                        cell.fg,
                    )))
                })
            }));

        let glyphs = &mut self.glyphs;
        let cursors = &mut self.cursors;
        let font = &self.font;
        let textgrid = &self.textgrid;
        let cell_size = &self.cell_size;

        // Process all cells and update the layer group as needed.
        for (order, column, row, content, rgb) in cells.into_iter() {
            let id = CellId { content, rgb };
            let order = u16::try_from(order).expect("too many layers");

            // Remove from old cells.
            self.old_cells.remove(&order);

            match self.cells.entry(order) {
                Entry::Occupied(entry) => {
                    if *entry.get() != id {
                        let raster = maybe_raster_for_cell_content(
                            render_context,
                            &id.content,
                            column,
                            row,
                            cell_size,
                            textgrid,
                            font,
                            glyphs,
                            cursors,
                        );
                        if let Some(raster) = raster {
                            let value = entry.into_mut();
                            *value = id;

                            self.new_cells.insert(order);
                            layer_group.insert(
                                order,
                                Layer {
                                    raster,
                                    clip: None,
                                    style: Style {
                                        fill_rule: FillRule::NonZero,
                                        fill: Fill::Solid(make_color(&rgb)),
                                        blend_mode: BlendMode::Over,
                                    },
                                },
                            );
                        } else {
                            entry.remove_entry();
                            layer_group.remove(order);
                        }
                    } else {
                        self.new_cells.insert(order);
                    }
                }
                Entry::Vacant(entry) => {
                    let raster = maybe_raster_for_cell_content(
                        render_context,
                        &id.content,
                        column,
                        row,
                        cell_size,
                        textgrid,
                        font,
                        glyphs,
                        cursors,
                    );
                    if let Some(raster) = raster {
                        entry.insert(id);
                        self.new_cells.insert(order);
                        layer_group.insert(
                            order,
                            Layer {
                                raster,
                                clip: None,
                                style: Style {
                                    fill_rule: FillRule::NonZero,
                                    fill: Fill::Solid(make_color(&rgb)),
                                    blend_mode: BlendMode::Over,
                                },
                            },
                        );
                    }
                }
            }
        }

        // Remove any remaining old cells.
        for order in self.old_cells.drain() {
            self.cells.remove(&order);
            layer_group.remove(order);
        }

        // Swap old cells for new cells.
        mem::swap(&mut self.old_cells, &mut self.new_cells);

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
