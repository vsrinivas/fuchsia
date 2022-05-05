// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::terminal::Terminal,
    std::cmp::{max, min},
};

pub struct Line {
    pub value: String,
    pub removed: bool,
}

impl Line {
    pub fn new(s: impl ToString) -> Self {
        Self { value: s.to_string(), removed: false }
    }

    pub fn removed(s: impl ToString) -> Self {
        Self { value: s.to_string(), removed: true }
    }

    pub fn len(&self) -> usize {
        self.value.len()
    }
}

pub struct Screen<T: Terminal> {
    pub terminal: T,
    pub lines: Vec<Line>,
    pub offset_top: usize,
    pub offset_left: usize,
    pub max_line_len: usize,
    pub filter_removed: bool,
}

impl<T: Terminal> Screen<T> {
    pub fn new(terminal: T, lines: Vec<Line>) -> Self {
        let max_line_len = lines.iter().map(|l| l.len()).max().unwrap_or(0);
        Screen {
            terminal,
            lines,
            offset_top: 0,
            offset_left: 0,
            max_line_len,
            filter_removed: false,
        }
    }

    pub fn set_lines(&mut self, lines: Vec<Line>) {
        self.max_line_len = lines.iter().map(|l| l.len()).max().unwrap_or(0);
        self.lines = lines;
        self.scroll(-(self.offset_top as i64), -(self.offset_left as i64));
    }

    pub fn max_lines(&self) -> i64 {
        let (_, h) = self.terminal.terminal_size();
        h as i64 - 2 // Leave 2 lines for info.
    }

    pub fn visible_line_count(&self) -> usize {
        self.lines.iter().filter(|l| !self.filter_removed || !l.removed).count()
    }

    pub fn set_filter_removed(&mut self, val: bool) {
        if self.filter_removed == val {
            return;
        }
        self.filter_removed = val;

        if self.filter_removed {
            // Starting to filter, tweak offset_top to remove offsets from newly filtered lines.
            self.offset_top -= self.lines.iter().take(self.offset_top).filter(|l| l.removed).count()
        }
    }

    // Scroll the visible layout, returns if the screen needs to be repainted.
    pub fn scroll(&mut self, down: i64, right: i64) -> bool {
        let (w, h) = self.terminal.terminal_size();

        let old_top = self.offset_top;
        let old_left = self.offset_left;

        self.offset_top = max(0, self.offset_top as i64 + down) as usize;
        self.offset_left = max(0, self.offset_left as i64 + right) as usize;

        // We should be careful not to scroll past the total number of lines,
        // hence we check that atleast the last h lines are visible on screen.
        self.offset_top =
            min(self.offset_top as i64, max(0, self.visible_line_count() as i64 - h as i64 + 2))
                as usize;

        // We should be careful not to scroll too much to the right,
        // hence we check that atleast the last w cols of the longest line
        // are visible on screen.
        self.offset_left =
            min(self.offset_left as i64, max(0, self.max_line_len as i64 - w as i64)) as usize;

        // If either offset_top or offset_left has changed, need to repaint screen.
        (old_top != self.offset_top) || (old_left != self.offset_left)
    }

    fn help_footer(&mut self) {
        let (_, h) = self.terminal.terminal_size();

        self.terminal.goto(1, h - 1);
        write!(
            self.terminal,
            "------------------- T: {}/{}, L: {}/{}",
            self.offset_top,
            self.visible_line_count(),
            self.offset_left,
            self.max_line_len,
        )
        .expect("Unable to write help footer to screen.");

        self.terminal.goto(1, h);
        write!(
            self.terminal,
            "Controls: [Q]uit. [R]efresh. {} filtered data. Arrow keys scroll.",
            if self.filter_removed { "S[h]ow" } else { "[H]ide" }
        )
        .expect("Unable to write last help line to screen.");
    }

    fn refresh(&mut self) {
        let (w, _) = self.terminal.terminal_size();
        let max_lines = self.max_lines() as usize;

        let filter_removed = self.filter_removed;

        let lines_to_output = self
            .lines
            .iter()
            .filter(|l| !filter_removed || !l.removed)
            .skip(self.offset_top)
            .take(max_lines);

        for (i, line) in lines_to_output.enumerate() {
            if self.offset_left < line.len() {
                let end = min(line.len(), self.offset_left + w as usize);
                if line.removed {
                    self.terminal.set_red_color();
                }

                self.terminal.goto(1, (i + 1) as u16);
                write!(
                    self.terminal,
                    "{:width$}",
                    &line.value[self.offset_left..end],
                    width = w as usize
                )
                .expect("Unable to write to terminal.");
                self.terminal.reset_color();
            } else {
                // Empty line
                write!(self.terminal, "{:width$}", ' ', width = w as usize)
                    .expect("Unable to write empty line to terminal.");
            }
        }
        self.help_footer();
    }

    pub fn refresh_screen_and_flush(&mut self) {
        self.terminal.goto(1, 1);
        self.refresh();
        self.terminal.flush().expect("Unable to flush to terminal.")
    }

    pub fn clear_screen(&mut self) {
        self.terminal.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_utils::{get_test_lines, FakeTerminal};

    #[fuchsia::test]
    fn small_screen_overflow() {
        let fake_terminal = FakeTerminal::new(9, 6);
        let mut screen = Screen::new(fake_terminal.clone(), get_test_lines());

        screen.refresh_screen_and_flush();

        // Only the first 4 lines will be visible, with 9 characters each.
        pretty_assertions::assert_eq!(
            fake_terminal.screen_without_help_footer(),
            "{        \nA long li\nShort lin\n         \n"
        );
    }

    #[fuchsia::test]
    fn screen_scroll() {
        let fake_terminal = FakeTerminal::new(9, 6);
        let mut screen = Screen::new(fake_terminal.clone(), get_test_lines());

        // Scroll down 4 times.
        screen.scroll(4, 0);

        // Scroll right 3 times.
        screen.scroll(0, 3);

        screen.refresh_screen_and_flush();

        pretty_assertions::assert_eq!(
            fake_terminal.screen_without_help_footer(),
            "ther long\nond Last \n last lin\n         \n",
        );

        // Scroll down 100 times.
        screen.scroll(100, 0);

        screen.refresh_screen_and_flush();

        // Output shouldn't change as we are already at max down scroll.
        pretty_assertions::assert_eq!(
            fake_terminal.screen_without_help_footer(),
            "ther long\nond Last \n last lin\n         \n",
        );

        // Scroll up 2 times.
        screen.scroll(-2, 0);

        // Scroll left 1 time.
        screen.scroll(0, -1);

        screen.refresh_screen_and_flush();

        pretty_assertions::assert_eq!(
            fake_terminal.screen_without_help_footer(),
            "ort line2\n         \nother lon\ncond Last\n",
        );
    }
}
