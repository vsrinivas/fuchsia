// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    carnelian::{
        Canvas, Color, Coord, FontDescription, FontFace, MappingPixelSink, Paint, Point, Rect, Size,
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

const MAXIMUM_THUMB_RATIO: f32 = 0.8;
const MINIMUM_THUMB_RATIO: f32 = 0.05;

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
    pub fn render(&self, canvas: &mut Canvas<MappingPixelSink>, context: ScrollContext) {
        ftrace::duration!("terminal", "Views:ScrollBar:render");
        if let Some(thumb_info) = context.calculate_thumb_render_info(self.frame.size.height) {
            let size = Size::new(self.frame.size.width, thumb_info.height);
            let origin = Point::new(
                self.frame.origin.x,
                self.frame.origin.y + self.frame.size.height
                    - thumb_info.vertical_offset
                    - thumb_info.height,
            );

            Self::draw_checkered_pattern(canvas, Rect::new(origin, size));
        }
    }

    fn draw_checkered_pattern(canvas: &mut Canvas<MappingPixelSink>, frame: Rect) {
        let size = Size::new(1.0, 1.0);
        let mut row = 0;
        let mut rect = Rect::new(frame.origin, size);
        while rect.origin.y < (frame.origin.y + frame.size.height - size.height) {
            let mut draw_black = if row % 2 == 0 { true } else { false };
            rect.origin.x = frame.origin.x;

            while rect.origin.x < (frame.origin.x + frame.size.width - size.width) {
                let color = if draw_black { Color::new() } else { Color::white() };
                canvas.fill_rect(&rect, color);
                draw_black = !draw_black;
                rect.origin.x += size.width;
            }
            rect.origin.y += size.height;
            row += 1;
        }
    }
}

pub struct ScrollContext {
    pub history: usize,
    pub visible_lines: usize,
    pub display_offset: usize,
}

#[derive(PartialEq, Debug)]
struct ScrollBarThumbRenderInfo {
    /// The height of the ScrollBarThumb
    height: Coord,

    /// The y position of the bottom of the ScrollBarThumb
    vertical_offset: Coord,
}

impl ScrollContext {
    /// Calculates the size to render the ScrollBar thumb.
    /// Will return None if no bar should be rendered.
    fn calculate_thumb_render_info(&self, height: Coord) -> Option<ScrollBarThumbRenderInfo> {
        if self.history == 0 {
            return None;
        }
        let total_lines = self.history + self.visible_lines;

        let thumb_height =
            Self::calculate_thumb_height_ratio(self.visible_lines as f32, total_lines as f32)
                * height;

        let vertical_offset =
            Self::calculate_pixels_per_line(height, thumb_height, self.history as f32)
                * (self.display_offset as f32);

        Some(ScrollBarThumbRenderInfo {
            height: thumb_height,
            vertical_offset: vertical_offset.floor(),
        })
    }

    fn calculate_thumb_height_ratio(visible_lines: f32, total_lines: f32) -> f32 {
        let ratio = visible_lines / total_lines;
        f32::min(f32::max(MINIMUM_THUMB_RATIO, ratio), MAXIMUM_THUMB_RATIO)
    }

    fn calculate_pixels_per_line(height: f32, thumb_size: f32, history: f32) -> f32 {
        (height - thumb_size) / history
    }
}

#[cfg(test)]
mod tests {
    use super::{ScrollBarThumbRenderInfo, ScrollContext};

    #[test]
    fn scroll_context_thumb_render_info_none_if_no_history() {
        let scroll_context = ScrollContext { history: 0, visible_lines: 100, display_offset: 0 };
        let render_info = scroll_context.calculate_thumb_render_info(1000.0);
        assert_eq!(render_info, None);
    }

    #[test]
    fn scroll_context_thumb_render_info_returns_proper_height() {
        let scroll_context = ScrollContext { history: 1, visible_lines: 1, display_offset: 0 };
        let render_info = scroll_context.calculate_thumb_render_info(1000.0).unwrap();
        assert_eq!(render_info.height, 500.0,);
    }

    #[test]
    fn calculate_thumb_height_ratio_pins_to_min() {
        let ratio = ScrollContext::calculate_thumb_height_ratio(100.0, 10_100.0);
        assert_eq!(ratio, super::MINIMUM_THUMB_RATIO);
    }

    #[test]
    fn calculate_thumb_height_ratio_pins_to_max() {
        let ratio = ScrollContext::calculate_thumb_height_ratio(100.0, 101.0);
        assert_eq!(ratio, super::MAXIMUM_THUMB_RATIO);
    }

    #[test]
    fn calculate_thumb_height_ratio() {
        let ratio = ScrollContext::calculate_thumb_height_ratio(10.0, 40.0);
        assert_eq!(ratio, 0.25);
    }

    #[test]
    fn calculate_thumb_vertical_offset_top() {
        let scroll_context = ScrollContext { history: 300, visible_lines: 2, display_offset: 300 };
        let render_info = scroll_context.calculate_thumb_render_info(100.0).unwrap();
        assert_eq!(render_info.vertical_offset, 95.0);
    }

    #[test]
    fn calculate_thumb_vertical_offset_mid() {
        let scroll_context = ScrollContext { history: 2, visible_lines: 2, display_offset: 1 };
        let render_info = scroll_context.calculate_thumb_render_info(100.0).unwrap();
        assert_eq!(render_info.vertical_offset, 25.0);
    }

    #[test]
    fn calculate_thumb_vertical_offset_with_round() {
        let scroll_context = ScrollContext { history: 1, visible_lines: 2, display_offset: 1 };
        let render_info = scroll_context.calculate_thumb_render_info(100.0).unwrap();
        assert_eq!(render_info.vertical_offset, 33.0);
    }

    #[test]
    fn calculate_thumb_vertical_offset_bottom() {
        let scroll_context = ScrollContext { history: 300, visible_lines: 2, display_offset: 0 };
        let render_info = scroll_context.calculate_thumb_render_info(100.0).unwrap();
        assert_eq!(render_info.vertical_offset, 0.0);
    }

    #[test]
    fn scroll_context_thumb_render_info_equality() {
        let first = ScrollBarThumbRenderInfo { height: 100.0, vertical_offset: 100.0 };
        let second = ScrollBarThumbRenderInfo { height: 100.0, vertical_offset: 100.0 };
        assert_eq!(first, second);
    }

    #[test]
    fn scroll_context_thumb_render_info_not_equal_diff_offset() {
        let first = ScrollBarThumbRenderInfo { height: 100.0, vertical_offset: 100.0 };
        let second = ScrollBarThumbRenderInfo { height: 100.0, vertical_offset: 0.0 };
        assert_ne!(first, second);
    }

    #[test]
    fn scroll_context_thumb_render_info_equality_not_equal_diff_height() {
        let first = ScrollBarThumbRenderInfo { height: 100.0, vertical_offset: 100.0 };
        let second = ScrollBarThumbRenderInfo { height: 10.0, vertical_offset: 100.0 };
        assert_ne!(first, second);
    }
}
