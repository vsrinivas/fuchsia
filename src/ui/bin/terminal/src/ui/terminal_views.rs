// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ui::TerminalMessages,
    carnelian::{make_message, AppContext, Coord, MessageTarget, Point, Rect, Size, ViewKey},
};

pub struct GridView {
    pub frame: Rect,
    pub cell_size: Size,
}

impl Default for GridView {
    fn default() -> Self {
        GridView { frame: Rect::zero(), cell_size: Size::zero() }
    }
}

const MAXIMUM_THUMB_RATIO: f32 = 0.8;
const MINIMUM_THUMB_RATIO: f32 = 0.05;

pub struct ScrollBar {
    pub frame: Rect,

    /// The content size of the scrollable area.
    pub content_height: Coord,

    /// The vertical distance that the content is offset from the bottom
    pub content_offset: Coord,

    /// The frame to draw the scroll bar thumb
    thumb_frame: Option<Rect>,

    /// Indicates whether we are tracking a scroll or not. This will
    /// eventually need to track the device_id when we handle multiple
    /// input events.
    pointer_tracking_start: Option<(Point, Coord)>,

    // AppContext used to update scroll thumb rendering.
    app_context: Option<AppContext>,
    view_key: ViewKey,
}

impl Default for ScrollBar {
    fn default() -> Self {
        ScrollBar {
            app_context: None,
            view_key: 0,
            frame: Rect::zero(),
            content_height: 0.0,
            content_offset: 0.0,
            thumb_frame: None,
            pointer_tracking_start: None,
        }
    }
}

impl ScrollBar {
    pub fn new(app_context: AppContext, view_key: ViewKey) -> Self {
        ScrollBar {
            app_context: Some(app_context),
            view_key,
            frame: Rect::zero(),
            content_height: 0.0,
            content_offset: 0.0,
            thumb_frame: None,
            pointer_tracking_start: None,
        }
    }

    pub fn thumb_frame(&self) -> Option<Rect> {
        self.thumb_frame
    }

    /// This method must be called after the client has updated
    /// the frame, content_height or content_offset. We leave this
    /// up to the caller to allow for the optimization of batching
    /// these updates without needing to recalculate the frame.
    pub fn invalidate_thumb_frame(&mut self) {
        self.update_thumb_frame();
    }

    pub fn begin_tracking_pointer_event(&mut self, point: Point) {
        if let Some(frame) = &self.thumb_frame {
            if !frame.contains(point) {
                // jump the middle of the thumb to the middle of the point
                let thumb_height = frame.size.height;
                let conversion_factor = self.pixel_space_to_content_space_conversion_factor();

                let proposed_offset = conversion_factor
                    * (self.frame.size.height
                        - (point.y - self.frame.origin.y)
                        - (thumb_height / 2.0));

                self.propose_offset(proposed_offset, conversion_factor, thumb_height);
            }
            let start_content_offset = self.content_offset;
            self.pointer_tracking_start = Some((point, start_content_offset));
        }
    }

    pub fn handle_pointer_move(&mut self, point: Point) {
        if let (Some((start_point, start_offset)), Some(thumb_frame)) =
            (self.pointer_tracking_start, self.thumb_frame)
        {
            let dy = start_point.y - point.y;
            let conversion_factor = self.pixel_space_to_content_space_conversion_factor();
            let proposed_offset = start_offset + (conversion_factor * dy);
            self.propose_offset(proposed_offset, conversion_factor, thumb_frame.size.height);
        }
    }

    pub fn cancel_pointer_event(&mut self) {
        self.pointer_tracking_start = None;
    }

    fn propose_offset(
        &mut self,
        proposed_offset: Coord,
        conversion_factor: f32,
        thumb_height: f32,
    ) {
        // we have some rounding errors which make us loose 2 pixels. We round our inputs
        // to get those pixels back when calculating the max_offset.
        let max_offset =
            f32::ceil((self.frame.size.height - f32::floor(thumb_height)) * conversion_factor);
        self.content_offset = Coord::min(Coord::max(proposed_offset, 0.0), max_offset);
        self.invalidate_thumb_frame();
    }

    #[inline]
    pub fn is_tracking(&self) -> bool {
        self.pointer_tracking_start.is_some()
    }

    fn update_thumb_frame(&mut self) {
        let thumb_frame = if let Some(thumb_info) = self.calculate_thumb_render_info() {
            let thumb_frame = thumb_info.calculate_frame_in_rect(&self.frame);

            Some(thumb_frame)
        } else {
            None
        };

        if self.thumb_frame != thumb_frame {
            self.thumb_frame = thumb_frame;
            if let Some(app_context) = &self.app_context {
                app_context.queue_message(
                    MessageTarget::View(self.view_key),
                    make_message(TerminalMessages::SetScrollThumbMessage(thumb_frame)),
                );
                app_context.request_render(self.view_key);
            }
        }
    }

    fn calculate_thumb_render_info(&self) -> Option<ThumbRenderInfo> {
        if self.content_height <= self.frame.size.height {
            return None;
        }

        let height =
            Self::calculate_thumb_height_ratio(self.frame.size.height, self.content_height)
                * self.frame.size.height;

        let vertical_offset =
            Coord::floor(self.content_space_to_pixel_space_factor(&height) * self.content_offset);
        Some(ThumbRenderInfo { height, vertical_offset })
    }

    fn calculate_thumb_height_ratio(frame_height: Coord, content_height: Coord) -> Coord {
        let ratio = frame_height / content_height;
        Coord::min(Coord::max(MINIMUM_THUMB_RATIO, ratio), MAXIMUM_THUMB_RATIO)
    }

    #[inline]
    fn pixel_space_to_content_space_conversion_factor(&self) -> Coord {
        // this method is different from the thumb_height_ratio in that it will never round
        // so it can be used to calculate offsets from pixel positions.
        self.content_height / self.frame.size.height
    }

    #[inline]
    fn content_space_to_pixel_space_factor(&self, thumb_height: &Coord) -> Coord {
        (self.frame.size.height - thumb_height) / (self.content_height - self.frame.size.height)
    }
}

#[derive(PartialEq, Debug)]
struct ThumbRenderInfo {
    /// The height of the ScrollBarThumb
    height: Coord,

    /// The y position of the bottom of the ScrollBarThumb
    vertical_offset: Coord,
}

impl ThumbRenderInfo {
    fn calculate_frame_in_rect(&self, outer_rect: &Rect) -> Rect {
        let size = Size::new(outer_rect.size.width, self.height);
        let origin = Point::new(
            outer_rect.origin.x,
            outer_rect.origin.y + outer_rect.size.height - self.vertical_offset - self.height,
        );
        Rect::new(origin, size)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rect_with_height(height: Coord) -> Rect {
        Rect::new(Point::zero(), Size::new(10.0, height))
    }

    #[test]
    fn sroll_bar_is_tracking_flag() {
        let mut scroll_bar = ScrollBar::default();

        assert_eq!(scroll_bar.is_tracking(), false);

        scroll_bar.pointer_tracking_start = Some((Point::new(0.0, 0.0), 0.0));
        assert_eq!(scroll_bar.is_tracking(), true);
    }

    #[test]
    fn scroll_bar_does_not_change_content_offset_if_not_tracking() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = rect_with_height(100.0);
        scroll_bar.content_height = 400.0;

        scroll_bar.handle_pointer_move(Point::new(50.0, 50.0));
        assert_eq!(scroll_bar.content_offset, 0.0);
    }

    #[test]
    fn scroll_bar_updates_content_offset_on_move_when_tracking() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = rect_with_height(100.0);
        scroll_bar.content_height = 400.0;
        scroll_bar.invalidate_thumb_frame();

        scroll_bar.pointer_tracking_start =
            Some((Point::new(10.0, 90.0), scroll_bar.content_offset));

        // a movement of 1 pixel in view space should equate to a movement
        // of 4 points in content space
        scroll_bar.handle_pointer_move(Point::new(10.0, 89.0));
        assert_eq!(scroll_bar.content_offset, 4.0);
    }

    #[test]
    fn scroll_bar_updates_content_offset_on_move_when_tracking_nonzero_origin() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = Rect::new(Point::new(10.0, 10.0), Size::new(10.0, 100.0));
        scroll_bar.content_height = 400.0;
        scroll_bar.invalidate_thumb_frame();

        scroll_bar.pointer_tracking_start =
            Some((Point::new(10.0, 90.0), scroll_bar.content_offset));

        // a movement of 1 pixel in view space should equate to a movement
        // of 4 points in content space
        scroll_bar.handle_pointer_move(Point::new(10.0, 89.0));
        assert_eq!(scroll_bar.content_offset, 4.0);
    }

    #[test]
    fn scroll_bar_updates_content_offset_on_move_when_tracking_stays_above_zero() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = rect_with_height(100.0);
        scroll_bar.content_height = 400.0;
        scroll_bar.invalidate_thumb_frame();

        scroll_bar.pointer_tracking_start =
            Some((Point::new(10.0, 90.0), scroll_bar.content_offset));

        scroll_bar.handle_pointer_move(Point::new(10.0, 91.0));
        assert_eq!(scroll_bar.content_offset, 0.0);
    }

    #[test]
    fn scroll_bar_updates_content_offset_on_move_when_tracking_does_not_exceed_maximum() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = rect_with_height(100.0);
        scroll_bar.content_height = 400.0;
        scroll_bar.invalidate_thumb_frame();

        scroll_bar.pointer_tracking_start =
            Some((Point::new(10.0, 90.0), scroll_bar.content_offset));

        scroll_bar.handle_pointer_move(Point::new(10.0, 0.0));
        assert_eq!(scroll_bar.content_offset, 300.0);
    }

    #[test]
    fn scroll_bar_begin_pointer_move_jumps_if_initial_point_outside_thumb_min() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = rect_with_height(100.0);
        scroll_bar.content_height = 400.0;
        scroll_bar.invalidate_thumb_frame();

        scroll_bar.begin_tracking_pointer_event(Point::new(5.0, 10.0));
        assert_eq!(scroll_bar.content_offset, 300.0);
    }

    #[test]
    fn scroll_bar_begin_pointer_move_jumps_if_initial_point_outside_thumb() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = rect_with_height(100.0);
        scroll_bar.content_height = 500.0;
        scroll_bar.invalidate_thumb_frame();

        scroll_bar.begin_tracking_pointer_event(Point::new(5.0, 50.0));
        assert_eq!(scroll_bar.content_offset, 200.0);
    }

    #[test]
    fn scroll_bar_begin_pointer_move_jumps_if_initial_point_outside_thumb_nonzero_origin() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = Rect::new(Point::new(10.0, 10.0), Size::new(10.0, 100.0));
        scroll_bar.content_height = 500.0;
        scroll_bar.invalidate_thumb_frame();

        scroll_bar.begin_tracking_pointer_event(Point::new(5.0, 60.0));
        assert_eq!(scroll_bar.content_offset, 200.0);
    }

    #[test]
    fn scroll_bar_begin_pointer_move_jumps_if_initial_point_outside_thumb_max() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = rect_with_height(100.0);
        scroll_bar.content_height = 400.0;
        scroll_bar.content_offset = 300.0;
        scroll_bar.invalidate_thumb_frame();

        scroll_bar.begin_tracking_pointer_event(Point::new(5.0, 99.0));
        assert_eq!(scroll_bar.content_offset, 0.0);
    }

    #[test]
    fn scroll_bar_cancel_pointer_event_drops_last_tracking_point() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = rect_with_height(100.0);
        scroll_bar.content_height = 400.0;
        scroll_bar.invalidate_thumb_frame();

        scroll_bar.begin_tracking_pointer_event(Point::new(10.0, 10.0));
        scroll_bar.cancel_pointer_event();
        assert!(scroll_bar.pointer_tracking_start.is_none());
    }

    #[test]
    fn thumb_frame_updated_when_told_thumb_frame_is_invalidated() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.content_height = 10_000.0;
        scroll_bar.invalidate_thumb_frame();
        assert!(scroll_bar.thumb_frame.is_some());
    }

    #[test]
    fn thumb_render_info_none_same_content_size_and_frame() {
        let scroll_bar = ScrollBar::default();
        let thumb_info = scroll_bar.calculate_thumb_render_info();
        assert!(thumb_info.is_none());
    }

    #[test]
    fn thumb_render_info_none_not_scrollable() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = rect_with_height(1000.0);
        scroll_bar.content_height = 900.0;

        let thumb_info = scroll_bar.calculate_thumb_render_info();
        assert!(thumb_info.is_none());
    }

    #[test]
    fn scroll_bar_thumb_render_info_returns_proper_height() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.content_height = 2_000.0;
        scroll_bar.frame.size.height = 1_000.0;

        let render_info = scroll_bar.calculate_thumb_render_info().unwrap();
        assert_eq!(render_info.height, 500.0,);
    }

    #[test]
    fn calculate_thumb_height_ratio_pins_to_min() {
        let ratio = ScrollBar::calculate_thumb_height_ratio(100.0, 10_100.0);
        assert_eq!(ratio, super::MINIMUM_THUMB_RATIO);
    }

    #[test]
    fn calculate_thumb_height_ratio_pins_to_max() {
        let ratio = ScrollBar::calculate_thumb_height_ratio(100.0, 101.0);
        assert_eq!(ratio, super::MAXIMUM_THUMB_RATIO);
    }

    #[test]
    fn calculate_thumb_height_ratio() {
        let ratio = ScrollBar::calculate_thumb_height_ratio(10.0, 40.0);
        assert_eq!(ratio, 0.25);
    }

    #[test]
    fn calculate_thumb_vertical_offset_top() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = rect_with_height(100.0);
        scroll_bar.content_height = 400.0;
        scroll_bar.content_offset = 300.0;

        let render_info = scroll_bar.calculate_thumb_render_info().unwrap();
        assert_eq!(render_info.vertical_offset, 75.0);
    }

    #[test]
    fn calculate_thumb_vertical_offset_mid() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = rect_with_height(100.0);
        scroll_bar.content_height = 400.0;
        scroll_bar.content_offset = 100.0;

        let render_info = scroll_bar.calculate_thumb_render_info().unwrap();

        assert_eq!(render_info.vertical_offset, 25.0);
    }

    #[test]
    fn calculate_thumb_vertical_offset_with_round() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = rect_with_height(100.0);
        scroll_bar.content_height = 300.0;
        scroll_bar.content_offset = 100.0;

        let render_info = scroll_bar.calculate_thumb_render_info().unwrap();

        assert_eq!(render_info.vertical_offset, 33.0);
    }

    #[test]
    fn calculate_thumb_vertical_offset_bottom() {
        let mut scroll_bar = ScrollBar::default();
        scroll_bar.frame = rect_with_height(100.0);
        scroll_bar.content_height = 302.0;
        scroll_bar.content_offset = 0.0;

        let render_info = scroll_bar.calculate_thumb_render_info().unwrap();

        assert_eq!(render_info.vertical_offset, 0.0);
    }

    #[test]
    fn scroll_context_thumb_render_info_equality() {
        let first = ThumbRenderInfo { height: 100.0, vertical_offset: 100.0 };
        let second = ThumbRenderInfo { height: 100.0, vertical_offset: 100.0 };
        assert_eq!(first, second);
    }

    #[test]
    fn scroll_context_thumb_render_info_not_equal_diff_offset() {
        let first = ThumbRenderInfo { height: 100.0, vertical_offset: 100.0 };
        let second = ThumbRenderInfo { height: 100.0, vertical_offset: 0.0 };
        assert_ne!(first, second);
    }

    #[test]
    fn scroll_context_thumb_render_info_equality_not_equal_diff_height() {
        let first = ThumbRenderInfo { height: 100.0, vertical_offset: 100.0 };
        let second = ThumbRenderInfo { height: 10.0, vertical_offset: 100.0 };
        assert_ne!(first, second);
    }

    #[test]
    fn thumb_render_info_calculate_frame_in_rect() {
        let thumb_info = ThumbRenderInfo { height: 10.0, vertical_offset: 10.0 };
        let outer = Rect::new(Point::new(10.0, 10.0), Size::new(10.0, 1000.0));
        let rect = thumb_info.calculate_frame_in_rect(&outer);

        assert_eq!(rect, Rect::new(Point::new(10.0, 990.0), Size::new(10.0, 10.0)));
    }
}
