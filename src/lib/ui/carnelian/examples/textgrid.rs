// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    carnelian::{
        app::Config,
        color::Color,
        drawing::{load_font, DisplayRotation, FontFace, GlyphMap, TextGrid, TextGridCell},
        make_app_assistant,
        render::{BlendMode, Context as RenderContext, Fill, FillRule, Layer, Style},
        scene::{
            facets::{Facet, FacetId},
            scene::{Scene, SceneBuilder},
            LayerGroup,
        },
        App, AppAssistant, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
    },
    fuchsia_trace::duration,
    fuchsia_trace_provider,
    fuchsia_zircon::Event,
    rustc_hash::FxHashMap,
    std::{
        any::Any,
        collections::hash_map::Entry,
        f32,
        fs::File,
        io::{prelude::*, BufReader},
        path::Path,
        path::PathBuf,
    },
};

/// Text Grid.
#[derive(Debug, FromArgs)]
#[argh(name = "textgrid_rs")]
struct Args {
    /// display rotatation
    #[argh(option)]
    rotation: Option<DisplayRotation>,

    /// text file to load (default is nyancat.txt)
    #[argh(option, default = "String::from(\"nyancat.txt\")")]
    file: String,

    /// background color (default is black)
    #[argh(option, from_str_fn(color_from_str))]
    background: Option<Color>,

    /// foreground color (default is white)
    #[argh(option, from_str_fn(color_from_str))]
    foreground: Option<Color>,

    /// cell size (default is 8x16)
    #[argh(option, from_str_fn(size_from_str))]
    cell_size: Option<Size>,

    /// cell padding (default is 2)
    #[argh(option, default = "2.0")]
    cell_padding: f32,
}

fn color_from_str(value: &str) -> Result<Color, String> {
    Color::from_hash_code(value).map_err(|err| err.to_string())
}

fn size_from_str(value: &str) -> Result<Size, String> {
    let pair: Vec<_> = value.splitn(2, "x").collect();
    let width = pair[0].parse::<f32>().map_err(|err| err.to_string())?;
    let height = pair[1].parse::<f32>().map_err(|err| err.to_string())?;
    Ok(Size::new(width, height))
}

struct TextGridAppAssistant {
    display_rotation: DisplayRotation,
    filename: String,
    background: Color,
    foreground: Color,
    cell_size: Size,
    cell_padding: f32,
}

impl Default for TextGridAppAssistant {
    fn default() -> Self {
        const BLACK_COLOR: Color = Color { r: 0, g: 0, b: 0, a: 255 };
        let args: Args = argh::from_env();
        let display_rotation = args.rotation.unwrap_or(DisplayRotation::Deg0);
        let filename = args.file;
        let background = args.background.unwrap_or(BLACK_COLOR);
        let foreground = args.foreground.unwrap_or(Color::white());
        let cell_size = args.cell_size.unwrap_or(Size::new(8.0, 16.0));
        let cell_padding = args.cell_padding;

        Self { display_rotation, filename, background, foreground, cell_size, cell_padding }
    }
}

impl AppAssistant for TextGridAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let filename = self.filename.clone();
        let background = self.background;
        let foreground = self.foreground;
        let cell_size = self.cell_size;
        let cell_padding = self.cell_padding;

        Ok(Box::new(TextGridViewAssistant::new(
            filename,
            background,
            foreground,
            cell_size,
            cell_padding,
        )))
    }

    fn filter_config(&mut self, config: &mut Config) {
        config.display_rotation = self.display_rotation;
    }
}

struct TextGridFacet {
    textgrid: TextGrid,
    font: FontFace,
    glyphs: GlyphMap,
    foreground: Color,
    pages: Vec<Vec<(u16, u16, char)>>,
    size: Size,
    current_page: usize,
    cells: FxHashMap<(u16, u16), char>,
}

/// Message used to advance to next page
struct NextPageMessage {}

impl TextGridFacet {
    fn new(
        font: FontFace,
        cell_size: Size,
        cell_padding: f32,
        foreground: Color,
        pages: Vec<Vec<(u16, u16, char)>>,
    ) -> Self {
        let textgrid = TextGrid::new(cell_size, cell_padding);
        let glyphs = GlyphMap::new();
        let cells: FxHashMap<(u16, u16), char> = FxHashMap::default();

        Self {
            textgrid,
            font,
            glyphs,
            foreground,
            pages,
            size: Size::zero(),
            current_page: 0,
            cells,
        }
    }
}

impl Facet for TextGridFacet {
    fn update_layers(
        &mut self,
        size: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        _view_context: &ViewAssistantContext,
    ) -> std::result::Result<(), anyhow::Error> {
        duration!("gfx", "TextGridFacet::update_layers");

        self.size = size;

        let glyphs = &mut self.glyphs;
        let font = &self.font;
        let textgrid = &self.textgrid;
        let foreground = &self.foreground;
        let page = &self.pages[self.current_page];

        const MAX_ROWS: u16 = 128;
        const MAX_COLUMNS_PER_ROW: u16 = 256;

        for (column, row, c) in page {
            assert_eq!(*row < MAX_ROWS, true);
            assert_eq!(*column < MAX_COLUMNS_PER_ROW, true);
            let order = *row * MAX_COLUMNS_PER_ROW + *column;
            match self.cells.entry((*column, *row)) {
                Entry::Occupied(entry) => {
                    if *entry.get() != *c {
                        let cell = TextGridCell::new(
                            render_context,
                            *column as usize,
                            *row as usize,
                            *c,
                            textgrid,
                            font,
                            glyphs,
                        );
                        if let Some(raster) = cell.raster {
                            let value = entry.into_mut();
                            *value = *c;

                            layer_group.insert(
                                order,
                                Layer {
                                    raster,
                                    clip: None,
                                    style: Style {
                                        fill_rule: FillRule::NonZero,
                                        fill: Fill::Solid(*foreground),
                                        blend_mode: BlendMode::Over,
                                    },
                                },
                            );
                        } else {
                            entry.remove_entry();
                            layer_group.remove(order);
                        }
                    }
                }
                Entry::Vacant(entry) => {
                    let cell = TextGridCell::new(
                        render_context,
                        *column as usize,
                        *row as usize,
                        *c,
                        textgrid,
                        font,
                        glyphs,
                    );
                    if let Some(raster) = cell.raster {
                        entry.insert(*c);
                        layer_group.insert(
                            order,
                            Layer {
                                raster,
                                clip: None,
                                style: Style {
                                    fill_rule: FillRule::NonZero,
                                    fill: Fill::Solid(*foreground),
                                    blend_mode: BlendMode::Over,
                                },
                            },
                        );
                    }
                }
            }
        }

        Ok(())
    }

    fn handle_message(&mut self, msg: Box<dyn Any>) {
        if let Some(_) = msg.downcast_ref::<NextPageMessage>() {
            self.current_page = (self.current_page + 1) % self.pages.len();
        }
    }

    fn calculate_size(&self, _available: Size) -> Size {
        self.size
    }
}

fn load_pages(path: PathBuf) -> Result<Vec<Vec<(u16, u16, char)>>, Error> {
    let file = File::open(path)?;
    let reader = BufReader::new(file);
    let mut pages = vec![];
    let mut cells = vec![];
    let mut row_start = 0;
    for (row, line) in reader.lines().enumerate() {
        if let Ok(line) = line {
            for (column, c) in line.chars().enumerate() {
                // 12 is form feed (a.k.a. page break).
                if c == 12 as char {
                    pages.push(cells.drain(..).collect());
                    row_start = row + 1;
                    break;
                } else {
                    cells.push((column as u16, (row - row_start) as u16, c));
                }
            }
        }
    }
    if !cells.is_empty() {
        pages.push(cells);
    }
    Ok(pages)
}

struct SceneDetails {
    scene: Scene,
    textgrid: FacetId,
}

struct TextGridViewAssistant {
    filename: String,
    background: Color,
    foreground: Color,
    cell_size: Size,
    cell_padding: f32,
    scene_details: Option<SceneDetails>,
}

impl TextGridViewAssistant {
    pub fn new(
        filename: String,
        background: Color,
        foreground: Color,
        cell_size: Size,
        cell_padding: f32,
    ) -> Self {
        Self { filename, background, foreground, cell_size, cell_padding, scene_details: None }
    }
}

impl ViewAssistant for TextGridViewAssistant {
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
            let font = load_font(PathBuf::from("/pkg/data/fonts/RobotoMono-Regular.ttf"))
                .expect("unable to load font data");
            let pages = load_pages(Path::new("/pkg/data/static").join(self.filename.clone()))
                .expect("unable to load text data");
            let mut builder = SceneBuilder::new().background_color(self.background).mutable(false);
            let textgrid_facet =
                TextGridFacet::new(font, self.cell_size, self.cell_padding, self.foreground, pages);
            let textgrid = builder.facet(Box::new(textgrid_facet));
            SceneDetails { scene: builder.build(), textgrid }
        });

        scene_details.scene.render(render_context, ready_event, context)?;
        scene_details.scene.send_message(&scene_details.textgrid, Box::new(NextPageMessage {}));

        self.scene_details = Some(scene_details);

        context.request_render();

        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    App::run(make_app_assistant::<TextGridAppAssistant>())
}
