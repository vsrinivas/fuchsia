// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    carnelian::{
        Canvas, Color, FontDescription, FontFace, MappingPixelSink, Paint, Point, Rect, Size,
    },
    fuchsia_trace as ftrace,
    term_model::{
        ansi::CursorStyle,
        term::{CursorKey, RenderableCellContent, RenderableCellsIter},
    },
};

const UNDERLINE_CURSOR_CHAR: char = '\u{10a3e2}';
const BEAM_CURSOR_CHAR: char = '\u{10a3e3}';
const BOX_CURSOR_CHAR: char = '\u{10a3e4}';

static FONT_DATA: &'static [u8] =
    include_bytes!("../../../../../prebuilt/third_party/fonts/robotomono/RobotoMono-Regular.ttf");

pub struct BackgroundView {
    pub color: Color,
    pub frame: Rect,
}

impl BackgroundView {
    pub fn new(color: Color) -> BackgroundView {
        BackgroundView { color: color, ..BackgroundView::default() }
    }

    pub fn render(&self, canvas: &mut Canvas<MappingPixelSink>) {
        ftrace::duration!("terminal", "Views:BackgroundView:render");
        canvas.fill_rect(&self.frame, self.color);
    }
}

impl Default for BackgroundView {
    fn default() -> Self {
        BackgroundView { color: Color::new(), frame: Rect::zero() }
    }
}

pub struct GridView {
    font: FontFace<'static>,
    pub frame: Rect,
    pub cell_size: Size,
}

impl Default for GridView {
    fn default() -> Self {
        GridView {
            frame: Rect::zero(),
            font: FontFace::new(FONT_DATA).expect("unable to load font data"),
            cell_size: Size::zero(),
        }
    }
}

impl GridView {
    pub fn render<'a, C>(
        &self,
        canvas: &mut Canvas<MappingPixelSink>,
        cells: RenderableCellsIter<'a, C>,
    ) {
        ftrace::duration!("terminal", "Views:GridView:render");
        let mut font_description = FontDescription { face: &self.font, size: 20, baseline: 18 };

        let size = self.cell_size;

        for cell in cells {
            let character = match maybe_char_for_renderable_cell_content(cell.inner) {
                Some(character) => character,
                None => continue,
            };

            let mut buffer = [0u8; 4];
            canvas.fill_text_cells(
                character.encode_utf8(&mut buffer),
                Point::new(size.width * cell.column.0 as f32, size.height * cell.line.0 as f32),
                size,
                &mut font_description,
                &Paint {
                    fg: Color { r: cell.fg.r, g: cell.fg.g, b: cell.fg.b, a: 0xFF },
                    bg: Color { r: cell.bg.r, g: cell.bg.g, b: cell.bg.b, a: 0xFF },
                },
            )
        }
    }
}

// The term-model library gives us zero-width characters in our array of chars. However,
// we do not support this at thsi point so we just pull out the first char for rendering.
fn maybe_char_for_renderable_cell_content(content: RenderableCellContent) -> Option<char> {
    match content {
        RenderableCellContent::Cursor(cursor_key) => chars_for_cursor(cursor_key),
        RenderableCellContent::Chars(chars) => Some(chars[0]),
    }
}

fn chars_for_cursor(cursor: CursorKey) -> Option<char> {
    match cursor.style {
        CursorStyle::Block => Some(BOX_CURSOR_CHAR),
        CursorStyle::Underline => Some(UNDERLINE_CURSOR_CHAR),
        CursorStyle::Beam => Some(BEAM_CURSOR_CHAR),
        //TODO add support for HollowBlock style
        CursorStyle::HollowBlock => Some(UNDERLINE_CURSOR_CHAR),
        CursorStyle::Hidden => None,
    }
}

pub struct ScrollBar {
    pub frame: Rect,
}

impl Default for ScrollBar {
    fn default() -> Self {
        ScrollBar { frame: Rect::zero() }
    }
}

impl ScrollBar {
    pub fn render(&self, _canvas: &mut Canvas<MappingPixelSink>) {
        ftrace::duration!("terminal", "Views:ScrollBar:render");
        // no implementation for rendering the scroll bar yet (fxb/36784)
    }
}
