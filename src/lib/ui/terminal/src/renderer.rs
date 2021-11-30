// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    carnelian::{
        color::Color,
        drawing::{FontFace, GlyphMap, TextGrid, TextGridCell},
        render::{
            BlendMode, Context as RenderContext, Fill, FillRule, Layer, Order, Path, Raster, Style,
        },
        scene::LayerGroup,
        Size,
    },
    euclid::point2,
    rustc_hash::{FxHashMap, FxHashSet},
    std::{collections::hash_map::Entry, convert::TryFrom, mem},
    term_model::{
        ansi::{CursorStyle, TermInfo},
        config::Config,
        term::{cell::Flags, color::Rgb, RenderableCell, RenderableCellContent, Term},
    },
};

pub fn font_to_cell_size(font_size: f32, cell_padding: f32) -> Size {
    let height = font_size + cell_padding;
    let width = height / 2.0;

    // Round to the smallest size equal or greater.
    Size::new(width, height).ceil()
}

#[derive(PartialEq, Debug)]
pub enum LayerContent {
    Cursor(CursorStyle),
    Char(char),
}

// The term-model library gives us zero-width characters in our array of chars. However,
// we do not support this at this point so we just pull out the first char for rendering.
impl From<RenderableCell> for LayerContent {
    fn from(cell: RenderableCell) -> Self {
        match cell.inner {
            RenderableCellContent::Cursor(cursor_key) => Self::Cursor(cursor_key.style),
            RenderableCellContent::Chars(chars) => {
                // Ignore hidden cells and render tabs as spaces to prevent font issues.
                if chars[0] == '\t' || cell.flags.contains(Flags::HIDDEN) {
                    Self::Char(' ')
                } else {
                    Self::Char(chars[0])
                }
            }
        }
    }
}

#[derive(PartialEq)]
struct LayerId {
    content: LayerContent,
    rgb: Rgb,
}

// Thickness of cursor lines is determined by multiplying thickness factor
// with the cell height. 1/16 has been chosen as that results in 1px thick
// lines for a 16px cell height.
const CURSOR_LINE_THICKNESS_FACTOR: f32 = 1.0 / 16.0;

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

fn maybe_raster_for_layer_content(
    render_context: &mut RenderContext,
    content: &LayerContent,
    column: usize,
    row: usize,
    cell_size: &Size,
    textgrid: &TextGrid,
    font: &FontFace,
    glyphs: &mut GlyphMap,
    cursors: &mut FxHashMap<CursorStyle, Option<Raster>>,
) -> Option<Raster> {
    match content {
        LayerContent::Cursor(cursor_style) => maybe_raster_for_cursor_style(
            render_context,
            *cursor_style,
            column,
            row,
            cell_size,
            cursors,
        ),
        LayerContent::Char(c) => {
            let grid_cell =
                TextGridCell::new(render_context, column, row, *c, textgrid, font, glyphs);
            grid_cell.raster
        }
    }
}

fn make_color(term_color: &Rgb) -> Color {
    Color { r: term_color.r, g: term_color.g, b: term_color.b, a: 0xff }
}

#[derive(PartialEq, Debug)]
pub struct RenderableLayer {
    pub order: usize,
    pub column: usize,
    pub row: usize,
    pub content: LayerContent,
    pub rgb: Rgb,
}

pub struct Offset {
    pub column: usize,
    pub row: usize,
}

pub fn renderable_layers<'b, T, C>(
    term: &'b Term<T>,
    config: &'b Config<C>,
    offset: &'b Offset,
) -> impl Iterator<Item = RenderableLayer> + 'b {
    let columns = term.cols().0;
    // renderable_cells() returns cells in painter's algorithm order, we
    // convert that into a retained scene by assuming that we have at most
    // 4 layers per cell:
    //
    // 1: Cursor background
    // 2: Cursor foreground
    // 3: Background
    // 4: Foreground
    let stride = columns * 4;
    term.renderable_cells(config).flat_map(move |cell| {
        let row = cell.line.0 + offset.row;
        let cell_order = row * stride + (cell.column.0 + offset.column) * 4;
        let content: LayerContent = cell.into();
        let order = match content {
            LayerContent::Cursor(_) => cell_order,
            LayerContent::Char(_) => cell_order + 2,
        };
        if cell.bg_alpha != 0.0 {
            assert!(cell.bg_alpha == 1.0, "unsupported bg_alpha: {}", cell.bg_alpha);
            Some(RenderableLayer {
                order: order,
                column: cell.column.0,
                row,
                content: LayerContent::Cursor(CursorStyle::Block),
                rgb: cell.bg,
            })
        } else {
            None
        }
        .into_iter()
        .chain(std::iter::once(RenderableLayer {
            order: order + 1,
            column: cell.column.0,
            row,
            content,
            rgb: cell.fg,
        }))
    })
}

pub struct Renderer {
    cell_size: Size,
    textgrid: TextGrid,
    glyphs: GlyphMap,
    cursors: FxHashMap<CursorStyle, Option<Raster>>,
    layers: FxHashMap<Order, LayerId>,
    old_layers: FxHashSet<Order>,
    new_layers: FxHashSet<Order>,
}

impl Renderer {
    pub fn new(font_size: f32, cell_padding: f32) -> Self {
        let cell_size = font_to_cell_size(font_size, cell_padding);
        let textgrid = TextGrid::new(cell_size, cell_padding);
        let glyphs = GlyphMap::new();
        let cursors = FxHashMap::default();
        let layers = FxHashMap::default();
        let old_layers = FxHashSet::default();
        let new_layers = FxHashSet::default();

        Self { cell_size, textgrid, glyphs, cursors, layers, old_layers, new_layers }
    }

    pub fn render<I>(
        &mut self,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        font: &FontFace,
        layers: I,
    ) where
        I: IntoIterator<Item = RenderableLayer>,
    {
        let glyphs = &mut self.glyphs;
        let cursors = &mut self.cursors;
        let textgrid = &self.textgrid;
        let cell_size = &self.cell_size;

        // Process all layers and update the layer group as needed.
        for RenderableLayer { order, column, row, content, rgb } in layers.into_iter() {
            let id = LayerId { content, rgb };
            let order = Order::try_from(order).unwrap_or_else(|e| panic!("{}", e));

            // Remove from old layers.
            self.old_layers.remove(&order);

            match self.layers.entry(order) {
                Entry::Occupied(entry) => {
                    if *entry.get() != id {
                        let raster = maybe_raster_for_layer_content(
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

                            let did_not_exist = self.new_layers.insert(order);
                            assert!(
                                did_not_exist,
                                "multiple layers with order: {}",
                                order.as_u32()
                            );
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
                        let did_not_exist = self.new_layers.insert(order);
                        assert!(did_not_exist, "multiple layers with order: {}", order.as_u32());
                    }
                }
                Entry::Vacant(entry) => {
                    let raster = maybe_raster_for_layer_content(
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
                        let did_not_exist = self.new_layers.insert(order);
                        assert!(did_not_exist, "multiple layers with order: {}", order.as_u32());
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

        // Remove any remaining old layers.
        for order in self.old_layers.drain() {
            self.layers.remove(&order);
            layer_group.remove(order);
        }

        // Swap old layers for new layers.
        mem::swap(&mut self.old_layers, &mut self.new_layers);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Error,
        carnelian::{
            drawing::{DisplayRotation, FontFace},
            render::{generic, Context as RenderContext, ContextInner},
        },
        euclid::size2,
        fuchsia_async as fasync,
        once_cell::sync::Lazy,
        std::collections::BTreeMap,
        term_model::{
            ansi::Processor,
            clipboard::Clipboard,
            event::{Event, EventListener},
            term::SizeInfo,
        },
    };

    struct TermConfig;

    impl Default for TermConfig {
        fn default() -> TermConfig {
            TermConfig
        }
    }

    struct EventProxy;

    impl EventListener for EventProxy {
        fn send_event(&self, _event: Event) {}
    }

    // This font creation method isn't ideal. The correct method would be to ask the Fuchsia
    // font service for the font data.
    static FONT_DATA: &'static [u8] = include_bytes!(
        "../../../../../prebuilt/third_party/fonts/robotomono/RobotoMono-Regular.ttf"
    );
    static FONT_FACE: Lazy<FontFace> =
        Lazy::new(|| FontFace::new(&FONT_DATA).expect("Failed to create font"));

    struct TestLayerGroup<'a>(&'a mut BTreeMap<Order, Layer>);

    impl LayerGroup for TestLayerGroup<'_> {
        fn clear(&mut self) {
            self.0.clear();
        }
        fn insert(&mut self, order: Order, layer: Layer) {
            self.0.insert(order, layer);
        }
        fn remove(&mut self, order: Order) {
            self.0.remove(&order);
        }
    }

    #[test]
    fn can_create_renderable_layers() -> Result<(), Error> {
        let cell_size = Size::new(8.0, 16.0);
        let size_info = SizeInfo {
            width: cell_size.width * 2.0,
            height: cell_size.height,
            cell_width: cell_size.width,
            cell_height: cell_size.height,
            padding_x: 0.0,
            padding_y: 0.0,
            dpr: 1.0,
        };
        let bg = Rgb { r: 0, g: 0, b: 0 };
        let fg = Rgb { r: 255, g: 255, b: 255 };
        let config = {
            let mut config = Config::<TermConfig>::default();
            config.colors.primary.background = bg;
            config.colors.primary.foreground = fg;
            config
        };
        let mut term = Term::new(&config, &size_info, Clipboard::new(), EventProxy {});
        let mut parser = Processor::new();
        let mut output = vec![];
        parser.advance(&mut term, 'A' as u8, &mut output);
        let offset = Offset { column: 0, row: 0 };
        let result = renderable_layers(&term, &config, &offset).collect::<Vec<_>>();
        assert_eq!(
            result,
            vec![
                RenderableLayer {
                    order: 3,
                    column: 0,
                    row: 0,
                    content: LayerContent::Char('A'),
                    rgb: fg
                },
                RenderableLayer {
                    order: 5,
                    column: 1,
                    row: 0,
                    content: LayerContent::Cursor(CursorStyle::Block),
                    rgb: fg
                },
                RenderableLayer {
                    order: 7,
                    column: 1,
                    row: 0,
                    content: LayerContent::Char(' '),
                    rgb: bg
                }
            ],
            "unexpected layers"
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_render_cell() {
        let size = size2(64, 64);
        let mold_context = generic::Mold::new_context_without_token(size, DisplayRotation::Deg0);
        let mut render_context = RenderContext { inner: ContextInner::Mold(mold_context) };
        let mut renderer = Renderer::new(14.0, 2.0);
        let layers = vec![
            RenderableLayer {
                order: 0,
                column: 0,
                row: 0,
                content: LayerContent::Cursor(CursorStyle::Block),
                rgb: Rgb { r: 0xff, g: 0xff, b: 0xff },
            },
            RenderableLayer {
                order: 1,
                column: 0,
                row: 0,
                content: LayerContent::Char('A'),
                rgb: Rgb { r: 0, g: 0, b: 0xff },
            },
        ];
        let mut result = BTreeMap::new();
        let mut layer_group = TestLayerGroup(&mut result);
        renderer.render(&mut layer_group, &mut render_context, &FONT_FACE, layers.into_iter());
        assert_eq!(result.len(), 2, "expected two layers");
    }
}
