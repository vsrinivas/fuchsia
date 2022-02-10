// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ui::{terminal_scroll_bar::ScrollBar, TerminalMessages},
    carnelian::{
        input::{self},
        make_message, AppSender, Coord, MessageTarget, Point, Rect, Size, ViewAssistantContext,
        ViewKey,
    },
    fuchsia_async as fasync, fuchsia_trace as ftrace,
};

// Hide scroll thumb after 1 second of scrolling or mouse leaving
// the scroll bar frame.
const HIDE_SCROLL_THUMB_DELAY: std::time::Duration = std::time::Duration::from_secs(1);

// Width of scroll bar frame. This is the width of the thumb when
// entering wide mode and is also the width of the area that can
// be used for scrolling using the mouse.
const SCROLL_BAR_FRAME_WIDTH: f32 = 16.0;

struct HideScrollThumbTimer {
    app_sender: Option<AppSender>,
    view_id: ViewKey,
    task: Option<fasync::Task<()>>,
    delay: std::time::Duration,
}

impl HideScrollThumbTimer {
    fn new(app_sender: AppSender, view_id: ViewKey, delay: std::time::Duration) -> Self {
        Self { app_sender: Some(app_sender), view_id, task: None, delay }
    }

    // Schedule task that will queue a scroll thumb message to hide the thumb.
    fn schedule(&mut self) {
        if let Some(app_sender) = &self.app_sender {
            let timer = fasync::Timer::new(fuchsia_async::Time::after(self.delay.into()));
            let app_sender = app_sender.clone();
            let view_id = self.view_id;
            let task = fasync::Task::local(async move {
                timer.await;
                app_sender.queue_message(
                    MessageTarget::View(view_id),
                    make_message(TerminalMessages::SetScrollThumbMessage(None)),
                );
                app_sender.request_render(view_id);
            });
            self.task = Some(task);
        }
    }

    // Cancel existing task. Returns true if task was scheduled.
    fn cancel(&mut self) -> bool {
        let task = self.task.take();
        if let Some(task) = task {
            fasync::Task::local(async move {
                task.cancel().await;
            })
            .detach();
            true
        } else {
            false
        }
    }

    // Returns true if task is currently scheduled.
    fn is_scheduled(&self) -> bool {
        self.task.is_some()
    }
}

impl Default for HideScrollThumbTimer {
    fn default() -> Self {
        HideScrollThumbTimer {
            app_sender: None,
            view_id: 0,
            task: None,
            delay: std::time::Duration::default(),
        }
    }
}

struct GridView {
    frame: Rect,
    cell_size: Size,
}

impl Default for GridView {
    fn default() -> Self {
        GridView { frame: Rect::zero(), cell_size: Size::zero() }
    }
}

pub struct TerminalScene {
    grid_view: GridView,
    scroll_bar: ScrollBar,
    size: Size,
    scroll_context: ScrollContext,
    active_pointer_id: Option<input::pointer::PointerId>,
    start_pointer_location: Point,
    hide_scroll_thumb_timer: HideScrollThumbTimer,
}

pub enum PointerEventResponse {
    /// Indicates that the pointer event resulted in the user wanting to scroll the grid
    ScrollLines(isize),

    /// Indicates that there are no changes in the grid but the view needs to be updated.
    /// This is usually in response to the scroll bar scrolling but not changing the lines.
    ViewDirty,
}

impl TerminalScene {
    pub fn new(app_sender: AppSender, view_key: ViewKey) -> Self {
        TerminalScene {
            size: Size::zero(),
            grid_view: GridView::default(),
            scroll_bar: ScrollBar::new(app_sender.clone(), view_key),
            scroll_context: ScrollContext::default(),
            active_pointer_id: None,
            start_pointer_location: Point::zero(),
            hide_scroll_thumb_timer: HideScrollThumbTimer::new(
                app_sender,
                view_key,
                HIDE_SCROLL_THUMB_DELAY,
            ),
        }
    }

    pub fn update_size(&mut self, new_size: Size, cell_size: Size) {
        ftrace::duration!("terminal", "Scene:TerminalScene:update_size");

        self.grid_view.cell_size = cell_size;
        self.size = new_size;

        self.grid_view.frame = Rect::from_size(new_size);

        let mut scroll_bar_frame = Rect::from_size(new_size);
        scroll_bar_frame.size.width = SCROLL_BAR_FRAME_WIDTH;
        scroll_bar_frame.origin.x = new_size.width - SCROLL_BAR_FRAME_WIDTH;
        self.scroll_bar.frame = scroll_bar_frame;

        self.update_scroll_metrics();
    }

    pub fn update_scroll_context(&mut self, scroll_context: ScrollContext) {
        if scroll_context != self.scroll_context {
            self.scroll_context = scroll_context;
            self.update_scroll_metrics();
        }
    }

    pub fn handle_mouse_event(
        &mut self,
        event: &input::mouse::Event,
        _ctx: &mut ViewAssistantContext,
    ) -> Option<PointerEventResponse> {
        let location = event.location.to_f32();

        // Let the scroll bar handle the event if it is currently
        // tracking the mouse pointer or if the location is in the
        // scroll bar frame.
        let result = if self.scroll_bar.is_tracking() || self.scroll_bar.frame.contains(location) {
            self.hide_scroll_thumb_timer.cancel();
            self.handle_primary_pointer_event_for_scroll_bar(&event)
        } else {
            None
        };

        // Schedule hiding of the scroll thumb if not already scheduled,
        // and we are not tracking the mouse, and the location of the
        // mouse is outside the scroll bar frame.
        if !self.scroll_bar.is_tracking() && !self.scroll_bar.frame.contains(location) {
            if !self.hide_scroll_thumb_timer.is_scheduled() {
                self.hide_scroll_thumb_timer.schedule();
            }
        }

        // Update dimming state when mouse location changes.
        self.scroll_bar.dim_thumb = !self.scroll_bar.thumb_contains(location);

        // Invalidate the scroll thumb after processing mouse events in
        // case the event caused the thumb to change.
        self.scroll_bar.invalidate_thumb();

        result
    }

    pub fn handle_touch_event(
        &mut self,
        touch_event: &input::touch::Event,
        _ctx: &mut ViewAssistantContext,
    ) -> Option<PointerEventResponse> {
        for contact in &touch_event.contacts {
            let event = input::pointer::Event::new_from_contact(contact);
            match event.phase {
                input::pointer::Phase::Down(point) => {
                    self.active_pointer_id = Some(event.pointer_id.clone());
                    self.start_pointer_location = point.to_f32();
                }
                input::pointer::Phase::Moved(location) => {
                    if Some(event.pointer_id.clone()) == self.active_pointer_id {
                        let location_offset = location.to_f32() - self.start_pointer_location;
                        fn div_and_trunc(value: f32, divisor: f32) -> isize {
                            (value / divisor).trunc() as isize
                        }
                        // Movement along Y-axis scrolls.
                        let cell_offset =
                            div_and_trunc(location_offset.y, self.grid_view.cell_size.height);
                        if cell_offset != 0 {
                            self.start_pointer_location.y +=
                                cell_offset as f32 * self.grid_view.cell_size.height;
                            return Some(PointerEventResponse::ScrollLines(cell_offset));
                        }
                    }
                }
                input::pointer::Phase::Up
                | input::pointer::Phase::Remove
                | input::pointer::Phase::Cancel => {
                    if Some(event.pointer_id.clone()) == self.active_pointer_id {
                        self.active_pointer_id = None;
                    }
                }
            }
        }
        None
    }

    pub fn show_scroll_thumb(&mut self) {
        // Cancel hiding of the scroll thumb.
        let was_scheduled = self.hide_scroll_thumb_timer.cancel();
        // Reschedule hiding of thumb if it was hidden or it was scheduled to be hidden.
        if self.scroll_bar.hidden_thumb || was_scheduled {
            self.hide_scroll_thumb_timer.schedule();
        }
        if self.scroll_bar.hidden_thumb {
            self.scroll_bar.hidden_thumb = false;
            self.scroll_bar.invalidate_thumb();
        }
    }

    pub fn hide_scroll_thumb(&mut self) {
        self.scroll_bar.hidden_thumb = true;
        // Disable wide thumb after hiding the scroll bar.
        self.scroll_bar.wide_thumb = false;
        self.scroll_bar.invalidate_thumb();
    }

    fn update_scroll_metrics(&mut self) {
        let total_lines = self.scroll_context.history + self.scroll_context.visible_lines;
        let content_height = self.grid_view.cell_size.height * (total_lines as Coord);

        let content_offset =
            self.grid_view.cell_size.height * (self.scroll_context.display_offset as Coord);

        self.scroll_bar.content_height = content_height;
        self.scroll_bar.content_offset = content_offset;
        self.scroll_bar.invalidate_thumb();
    }

    fn handle_primary_pointer_event_for_scroll_bar(
        &mut self,
        event: &input::mouse::Event,
    ) -> Option<PointerEventResponse> {
        let prev_offset = self.scroll_bar.content_offset;
        match &event.phase {
            input::mouse::Phase::Down(button) if button.is_primary() => {
                if !self.scroll_bar.is_tracking() {
                    self.scroll_bar.begin_tracking_pointer_event(event.location.to_f32());
                }
            }
            input::mouse::Phase::Moved => {
                if self.scroll_bar.is_tracking() {
                    self.scroll_bar.handle_pointer_move(event.location.to_f32());
                }
            }
            input::mouse::Phase::Up(button) if button.is_primary() => {
                if self.scroll_bar.is_tracking() {
                    self.scroll_bar.cancel_pointer_event();
                }
            }
            _ => {}
        };

        // do not rely on the ScrollContext being udpated at this point. Rely on the
        // values stored in the scroll bar to determine how many rows we have changed.
        let offset_change = Self::calculate_change_in_scroll_offset(
            prev_offset,
            self.scroll_bar.content_offset,
            self.grid_view.cell_size.height,
        );

        // Make sure we show a wide scroll thumb after processing a pointer event for
        // the scroll bar.
        if offset_change != 0 || self.scroll_bar.hidden_thumb || !self.scroll_bar.wide_thumb {
            self.scroll_bar.hidden_thumb = false;
            self.scroll_bar.wide_thumb = true;
        }

        match offset_change {
            0 => Some(PointerEventResponse::ViewDirty),
            _ => Some(PointerEventResponse::ScrollLines(offset_change)),
        }
    }

    fn calculate_change_in_scroll_offset(
        previous_content_offset: f32,
        content_offset: f32,
        row_height: f32,
    ) -> isize {
        let new_row = f32::floor(content_offset / row_height);
        let old_row = f32::floor(previous_content_offset / row_height);
        (new_row - old_row) as isize
    }
}

impl Default for TerminalScene {
    fn default() -> Self {
        TerminalScene {
            size: Size::zero(),
            grid_view: GridView::default(),
            scroll_bar: ScrollBar::default(),
            scroll_context: ScrollContext::default(),
            active_pointer_id: None,
            start_pointer_location: Point::zero(),
            hide_scroll_thumb_timer: HideScrollThumbTimer::default(),
        }
    }
}

#[derive(Eq, PartialEq)]
pub struct ScrollContext {
    pub history: usize,
    pub visible_lines: usize,
    pub display_offset: usize,
}

impl Default for ScrollContext {
    fn default() -> Self {
        ScrollContext { history: 0, visible_lines: 0, display_offset: 0 }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, carnelian::Point};

    #[test]
    fn calculate_difference_in_display_offset_no_change() {
        let row_height = 10.0;
        let content_offset = 10.1;
        let prev_offset = 10.0;

        // jump from row 1 to row 1
        let delta = TerminalScene::calculate_change_in_scroll_offset(
            prev_offset,
            content_offset,
            row_height,
        );
        assert_eq!(delta, 0);
    }

    #[test]
    fn calculate_difference_in_display_offset_single_line() {
        let row_height = 10.0;
        let content_offset = 20.1;
        let prev_offset = 10.0;

        // jump from row 1 to row 2
        let delta = TerminalScene::calculate_change_in_scroll_offset(
            prev_offset,
            content_offset,
            row_height,
        );
        assert_eq!(delta, 1);
    }

    #[test]
    fn calculate_difference_in_display_offset_down_single_line() {
        let row_height = 10.0;
        let content_offset = 19.0;
        let prev_offset = 20.0;

        // jump from row 2 to row 1
        let delta = TerminalScene::calculate_change_in_scroll_offset(
            prev_offset,
            content_offset,
            row_height,
        );
        assert_eq!(delta, -1);
    }

    #[test]
    fn calculate_difference_in_display_offset_multiple_lines() {
        let row_height = 10.0;
        let content_offset = 25.0;
        let prev_offset = 0.0;

        // jump from row 0 to row 2
        let delta = TerminalScene::calculate_change_in_scroll_offset(
            prev_offset,
            content_offset,
            row_height,
        );
        assert_eq!(delta, 2);
    }

    #[test]
    fn update_cell_size_updates_grid_view() {
        let mut scene = TerminalScene::default();
        scene.update_size(Size::zero(), Size::new(99.0, 99.0));
        assert_eq!(scene.grid_view.cell_size, Size::new(99.0, 99.0));
    }

    #[test]
    fn update_size_sets_frames() {
        let mut scene = TerminalScene::default();
        let size = Size::new(100.0, 100.0);
        scene.update_size(size, Size::zero());

        assert_eq!(scene.grid_view.frame, Rect::new(Point::zero(), Size::new(100.0, 100.0)));
        assert_eq!(
            scene.scroll_bar.frame,
            Rect::new(Point::new(84.0, 0.0), Size::new(16.0, 100.0))
        );
    }

    #[test]
    fn update_scroll_context_updates_content_height() {
        let mut scene = TerminalScene::default();
        scene.update_size(Size::zero(), Size::new(10.0, 10.0));
        assert_eq!(scene.scroll_bar.content_height, 0.0);

        let scroll_context = ScrollContext { history: 300, visible_lines: 2, display_offset: 0 };

        scene.update_scroll_context(scroll_context);
        assert_eq!(scene.scroll_bar.content_height, 3020.0);
    }

    #[test]
    fn update_scroll_context_updates_content_offset() {
        let mut scene = TerminalScene::default();
        scene.update_size(Size::zero(), Size::new(10.0, 10.0));
        let scroll_context = ScrollContext { history: 300, visible_lines: 2, display_offset: 10 };

        scene.update_scroll_context(scroll_context);
        assert_eq!(scene.scroll_bar.content_offset, 100.0);
    }

    #[test]
    fn update_cell_size_updates_content_size() {
        let mut scene = TerminalScene::default();

        let scroll_context = ScrollContext { history: 300, visible_lines: 2, display_offset: 0 };

        scene.update_scroll_context(scroll_context);

        scene.update_size(Size::zero(), Size::new(10.0, 5.0));
        assert_eq!(scene.scroll_bar.content_height, 1510.0);
    }

    #[test]
    fn update_cell_size_updates_content_offset() {
        let mut scene = TerminalScene::default();

        // originally frame is much bigger than content so there is no offset
        scene.update_size(Size::zero(), Size::new(10.0, 10.0));
        let scroll_context = ScrollContext { history: 10, visible_lines: 10, display_offset: 10 };
        scene.update_scroll_context(scroll_context);

        assert_eq!(scene.scroll_bar.content_offset, 100.0);

        scene.update_size(Size::new(100.0, 1000.0), Size::new(10.0, 5.0));
        assert_eq!(scene.scroll_bar.content_offset, 50.0);
    }
}
