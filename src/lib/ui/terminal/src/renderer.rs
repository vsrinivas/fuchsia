// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::paths::{
        maybe_path_for_char, maybe_path_for_cursor_style, path_for_strikeout, path_for_underline,
    },
    carnelian::{
        color::Color,
        drawing::{FontFace, Glyph, TextGrid},
        render::{BlendMode, Context as RenderContext, Fill, FillRule, Layer, Raster, Style},
        scene::{LayerGroup, SceneOrder},
        Size,
    },
    euclid::{point2, Rect},
    rustc_hash::{FxHashMap, FxHashSet},
    std::{
        collections::{hash_map::Entry, BTreeSet},
        convert::TryFrom,
        mem,
    },
    term_model::{
        ansi::{CursorStyle, TermInfo},
        config::Config,
        term::{cell::Flags, color::Rgb, RenderableCell, RenderableCellContent, Term},
    },
};

// Supported scale factors.
//
// These values are hard-coded in order to ensure that we use a grid size
// that is efficient and aligns with physical pixels.
const SCALE_FACTORS: &[f32] = &[1.0, 1.25, 2.0, 3.0, 4.0];

/// Returns a scale factor given a set of DPI buckets and an actual DPI value.
pub fn get_scale_factor(dpi: &BTreeSet<u32>, actual_dpi: f32) -> f32 {
    let mut scale_factor = 0;
    for value in dpi.iter() {
        if *value as f32 > actual_dpi {
            break;
        }
        scale_factor += 1;
    }
    *SCALE_FACTORS.get(scale_factor).unwrap_or(SCALE_FACTORS.last().unwrap())
}

/// Returns the cell size given a cell height.
pub fn cell_size_from_cell_height(font_set: &FontSet, height: f32) -> Size {
    let rounded_height = height.round();

    // Use a cell width that matches the horizontal advance of character
    // '0' as closely as possible. This minimizes the amount of horizontal
    // stretching used for glyph outlines. Fallback to half of cell height
    // if glyph '0' is missing.
    let face = &font_set.font.face;
    let width = face.glyph_index('0').map_or(height / 2.0, |glyph_index| {
        let ascent = face.ascender() as f32;
        let descent = face.descender() as f32;
        let horizontal_advance =
            face.glyph_hor_advance(glyph_index).expect("glyph_hor_advance") as f32;
        rounded_height * horizontal_advance / (ascent - descent)
    });

    Size::new(width.round(), rounded_height)
}

#[derive(Clone)]
pub struct FontSet {
    font: FontFace,
    bold_font: Option<FontFace>,
    italic_font: Option<FontFace>,
    bold_italic_font: Option<FontFace>,
    fallback_fonts: Vec<FontFace>,
}

impl FontSet {
    pub fn new(
        font: FontFace,
        bold_font: Option<FontFace>,
        italic_font: Option<FontFace>,
        bold_italic_font: Option<FontFace>,
        fallback_fonts: Vec<FontFace>,
    ) -> Self {
        Self { font, bold_font, italic_font, bold_italic_font, fallback_fonts }
    }
}

#[derive(PartialEq, Eq, Hash, Clone, Copy, Debug)]
pub enum LayerContent {
    Cursor(CursorStyle),
    Char((char, Flags)),
}

// The term-model library gives us zero-width characters in our array of chars. However,
// we do not support this at this point so we just pull out the first char for rendering.
impl From<RenderableCell> for LayerContent {
    fn from(cell: RenderableCell) -> Self {
        match cell.inner {
            RenderableCellContent::Cursor(cursor_key) => Self::Cursor(cursor_key.style),
            RenderableCellContent::Chars(chars) => {
                let flags = cell.flags & (Flags::BOLD_ITALIC | Flags::UNDERLINE | Flags::STRIKEOUT);
                // Ignore hidden cells and render tabs as spaces to prevent font issues.
                if chars[0] == '\t' || cell.flags.contains(Flags::HIDDEN) {
                    Self::Char((' ', flags))
                } else {
                    Self::Char((chars[0], flags))
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

fn maybe_raster_for_cursor_style(
    render_context: &mut RenderContext,
    cursor_style: CursorStyle,
    cell_size: &Size,
) -> Option<Raster> {
    maybe_path_for_cursor_style(render_context, cursor_style, cell_size).as_ref().map(|p| {
        let mut raster_builder = render_context.raster_builder().expect("raster_builder");
        raster_builder.add(p, None);
        raster_builder.build()
    })
}

fn maybe_fallback_glyph_for_char(
    render_context: &mut RenderContext,
    c: char,
    cell_size: &Size,
) -> Option<Glyph> {
    maybe_path_for_char(render_context, c, cell_size).as_ref().map(|p| {
        let mut raster_builder = render_context.raster_builder().expect("raster_builder");
        raster_builder.add(p, None);
        let raster = raster_builder.build();
        let bounding_box = Rect::from_size(*cell_size);
        Glyph { raster, bounding_box }
    })
}

fn maybe_glyph_for_char(
    context: &mut RenderContext,
    c: char,
    flags: Flags,
    textgrid: &TextGrid,
    font_set: &FontSet,
) -> Option<Glyph> {
    let maybe_bold_italic_font = match flags & Flags::BOLD_ITALIC {
        Flags::BOLD => font_set.bold_font.as_ref(),
        Flags::ITALIC => font_set.italic_font.as_ref(),
        Flags::BOLD_ITALIC => font_set.bold_italic_font.as_ref(),
        _ => None,
    };
    let scale = textgrid.scale;
    let offset = textgrid.offset;

    // Glyph search order:
    //
    // 1. Bold/italic font first if appropriate.
    // 2. Regular font.
    // 3. Fallback fonts.
    //
    // The fallback font can be used to provide icons/emojis
    // that are not expected to be part of the regular font.
    for font in maybe_bold_italic_font
        .iter()
        .map(|font| *font)
        .chain(std::iter::once(&font_set.font))
        .chain(font_set.fallback_fonts.iter())
    {
        if let Some(glyph_index) = font.face.glyph_index(c) {
            let glyph = Glyph::with_scale_and_offset(context, font, scale, offset, glyph_index);
            return Some(glyph);
        }
    }

    // Try fallback glyph if we failed to locate glyph in fonts.
    maybe_fallback_glyph_for_char(context, c, &textgrid.cell_size)
}

fn maybe_raster_for_char(
    context: &mut RenderContext,
    c: char,
    flags: Flags,
    textgrid: &TextGrid,
    font_set: &FontSet,
) -> Option<Raster> {
    // Get a potential glyph for this character.
    let maybe_glyph = maybe_glyph_for_char(context, c, flags, textgrid, font_set);

    // Create an extra raster if underline or strikeout flag is set.
    let maybe_extra_raster = if flags.intersects(Flags::UNDERLINE | Flags::STRIKEOUT) {
        let mut raster_builder = context.raster_builder().expect("raster_builder");
        if flags.contains(Flags::UNDERLINE) {
            // TODO(fxbug.dev/90967): Avoid glyph overlap and get position/thickness from font.
            raster_builder.add(&path_for_underline(&textgrid.cell_size, context), None);
        }
        if flags.contains(Flags::STRIKEOUT) {
            // TODO(fxbug.dev/90967): Get position/thickness from font.
            raster_builder.add(&path_for_strikeout(&textgrid.cell_size, context), None);
        }
        Some(raster_builder.build())
    } else {
        None
    };

    // Return a union of glyph raster and extra raster.
    match (maybe_glyph, maybe_extra_raster) {
        (Some(glyph), Some(extra_raster)) => Some(glyph.raster + extra_raster),
        (Some(glyph), None) => Some(glyph.raster),
        (None, Some(extra_raster)) => Some(extra_raster),
        _ => None,
    }
}

fn maybe_raster_for_layer_content(
    render_context: &mut RenderContext,
    content: &LayerContent,
    column: usize,
    row: usize,
    textgrid: &TextGrid,
    font_set: &FontSet,
    raster_cache: &mut FxHashMap<LayerContent, Option<Raster>>,
) -> Option<Raster> {
    raster_cache
        .entry(*content)
        .or_insert_with(|| match content {
            LayerContent::Cursor(cursor_style) => {
                maybe_raster_for_cursor_style(render_context, *cursor_style, &textgrid.cell_size)
            }
            LayerContent::Char((c, flags)) => {
                maybe_raster_for_char(render_context, *c, *flags, textgrid, font_set)
            }
        })
        .as_ref()
        .map(|r| {
            let cell_size = &textgrid.cell_size;
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
        let cell_order = row * stride + (cell.column.0 + offset.column);
        let content: LayerContent = cell.into();
        let order = match content {
            LayerContent::Cursor(_) => cell_order,
            LayerContent::Char(_) => cell_order + columns * 2,
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
            order: order + columns,
            column: cell.column.0,
            row,
            content,
            rgb: cell.fg,
        }))
    })
}

pub struct Renderer {
    textgrid: TextGrid,
    raster_cache: FxHashMap<LayerContent, Option<Raster>>,
    layers: FxHashMap<SceneOrder, LayerId>,
    old_layers: FxHashSet<SceneOrder>,
    new_layers: FxHashSet<SceneOrder>,
}

impl Renderer {
    pub fn new(font_set: &FontSet, cell_size: &Size) -> Self {
        let textgrid = TextGrid::new(&font_set.font, cell_size);
        let raster_cache = FxHashMap::default();
        let layers = FxHashMap::default();
        let old_layers = FxHashSet::default();
        let new_layers = FxHashSet::default();

        Self { textgrid, raster_cache, layers, old_layers, new_layers }
    }

    pub fn render<I>(
        &mut self,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        font_set: &FontSet,
        layers: I,
    ) where
        I: IntoIterator<Item = RenderableLayer>,
    {
        let raster_cache = &mut self.raster_cache;
        let textgrid = &self.textgrid;

        // Process all layers and update the layer group as needed.
        for RenderableLayer { order, column, row, content, rgb } in layers.into_iter() {
            let id = LayerId { content, rgb };
            let order = SceneOrder::try_from(order).unwrap_or_else(|e| panic!("{}", e));

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
                            textgrid,
                            font_set,
                            raster_cache,
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
                        textgrid,
                        font_set,
                        raster_cache,
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
    static FONT_SET: Lazy<FontSet> = Lazy::new(|| {
        FontSet::new(
            FontFace::new(&FONT_DATA).expect("Failed to create font"),
            None,
            None,
            None,
            vec![],
        )
    });

    struct TestLayerGroup<'a>(&'a mut BTreeMap<SceneOrder, Layer>);

    impl LayerGroup for TestLayerGroup<'_> {
        fn clear(&mut self) {
            self.0.clear();
        }
        fn insert(&mut self, order: SceneOrder, layer: Layer) {
            self.0.insert(order, layer);
        }
        fn remove(&mut self, order: SceneOrder) {
            self.0.remove(&order);
        }
    }

    #[test]
    fn check_scale_factors() {
        let dpi = BTreeSet::from([160, 240, 320]);
        assert_eq!(get_scale_factor(&dpi, 100.0), 1.0);
        assert_eq!(get_scale_factor(&dpi, 180.0), 1.25);
        assert_eq!(get_scale_factor(&dpi, 240.0), 2.0);
        assert_eq!(get_scale_factor(&dpi, 319.0), 2.0);
        assert_eq!(get_scale_factor(&dpi, 400.0), 3.0);
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
                    order: 6,
                    column: 0,
                    row: 0,
                    content: LayerContent::Char(('A', Flags::empty())),
                    rgb: fg
                },
                RenderableLayer {
                    order: 3,
                    column: 1,
                    row: 0,
                    content: LayerContent::Cursor(CursorStyle::Block),
                    rgb: fg
                },
                RenderableLayer {
                    order: 7,
                    column: 1,
                    row: 0,
                    content: LayerContent::Char((' ', Flags::empty())),
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
        let mut renderer = Renderer::new(&FONT_SET, &Size::new(8.0, 16.0));
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
                content: LayerContent::Char(('A', Flags::empty())),
                rgb: Rgb { r: 0, g: 0, b: 0xff },
            },
        ];
        let mut result = BTreeMap::new();
        let mut layer_group = TestLayerGroup(&mut result);
        renderer.render(&mut layer_group, &mut render_context, &FONT_SET, layers.into_iter());
        assert_eq!(result.len(), 2, "expected two layers");
    }
}
