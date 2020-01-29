// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]
use {
    crate::ui::terminal_views::{BackgroundView, GridView, ScrollBar},
    carnelian::{Canvas, Color, MappingPixelSink, Rect, Size},
    fuchsia_trace as ftrace,
    term_model::term::RenderableCellsIter,
};

pub struct TerminalScene {
    background_view: BackgroundView,
    grid_view: GridView,
    scroll_bar: ScrollBar,
    size: Size,
}

const SCROLL_BAR_WIDTH: f32 = 10.0;

impl TerminalScene {
    pub fn new(backgrond_color: Color) -> TerminalScene {
        TerminalScene {
            background_view: BackgroundView::new(backgrond_color),
            ..TerminalScene::default()
        }
    }

    pub fn update_background_color(&mut self, new_color: Color) {
        self.background_view.color = new_color;
    }

    pub fn update_cell_size(&mut self, new_size: Size) {
        self.grid_view.cell_size = new_size;
    }

    pub fn calculate_term_size_from_size(size: &Size) -> Size {
        Size::new(size.width - SCROLL_BAR_WIDTH, size.height)
    }

    pub fn render<'a, C>(
        &self,
        canvas: &mut Canvas<MappingPixelSink>,
        cells: RenderableCellsIter<'a, C>,
    ) {
        ftrace::duration!("terminal", "Scene:TerminalScene:render");
        self.background_view.render(canvas);
        self.grid_view.render(canvas, cells);
        self.scroll_bar.render(canvas);
    }

    pub fn update_size(&mut self, new_size: Size) {
        ftrace::duration!("terminal", "Scene:TerminalScene:update_size");
        self.size = new_size;
        self.background_view.frame = Rect::from_size(new_size);

        self.grid_view.frame =
            Rect::from_size(TerminalScene::calculate_term_size_from_size(&new_size));

        // for now just simply put the scroll bar over the top of the text
        let mut scroll_bar_frame = Rect::from_size(new_size);
        let scroll_bar_width = 10.0;
        scroll_bar_frame.size.width = scroll_bar_width;
        scroll_bar_frame.origin.x = new_size.width - scroll_bar_width;
        self.scroll_bar.frame = scroll_bar_frame;
    }
}

impl Default for TerminalScene {
    fn default() -> Self {
        TerminalScene {
            background_view: BackgroundView::default(),
            size: Size::zero(),
            grid_view: GridView::default(),
            scroll_bar: ScrollBar::default(),
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, carnelian::Point};

    #[test]
    fn new_with_color_sets_background_color() {
        let scene = TerminalScene::new(Color { r: 255, ..Color::new() });
        assert_eq!(scene.background_view.color.r, 255);
    }

    #[test]
    fn update_background_color_sets_color_of_bg_view() {
        let mut scene = TerminalScene::default();
        scene.update_background_color(Color { g: 255, ..Color::new() });
        assert_eq!(scene.background_view.color.g, 255);
    }

    #[test]
    fn update_cell_size_updates_grid_view() {
        let mut scene = TerminalScene::default();
        scene.update_cell_size(Size::new(99.0, 99.0));
        assert_eq!(scene.grid_view.cell_size, Size::new(99.0, 99.0));
    }

    #[test]
    fn calculate_term_size_has_affordance_for_scroll_bar() {
        let size = Size::new(100.0, 100.0);
        let calculated = TerminalScene::calculate_term_size_from_size(&size);

        assert_eq!(calculated.width, 90.0);
        assert_eq!(calculated.height, 100.0);
    }

    #[test]
    fn update_size_sets_frames() {
        let mut scene = TerminalScene::default();
        let size = Size::new(100.0, 100.0);
        scene.update_size(size);

        assert_eq!(scene.background_view.frame, Rect::new(Point::zero(), Size::new(100.0, 100.0)));
        assert_eq!(scene.grid_view.frame, Rect::new(Point::zero(), Size::new(90.0, 100.0)));
        assert_eq!(
            scene.scroll_bar.frame,
            Rect::new(Point::new(90.0, 0.0), Size::new(10.0, 100.0))
        );
    }
}
