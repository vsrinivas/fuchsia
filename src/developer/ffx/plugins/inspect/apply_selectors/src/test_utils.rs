// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{screen::Line, terminal::Terminal},
    std::{
        cmp::min,
        io::Write,
        sync::{Arc, Mutex},
    },
};

#[derive(Debug, Clone)]
pub struct FakeTerminal {
    w: u16,
    h: u16,
    // buffer is w x h screen, cursor is cursor position.
    buffer: Arc<Mutex<Vec<u8>>>,
    cursor: usize,
}

impl Write for FakeTerminal {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        let len = buf.len();
        let mut inner = self.buffer.lock().unwrap();
        let end = min(self.cursor + len, inner.len());

        inner[self.cursor..end].copy_from_slice(&buf[..end - self.cursor]);

        // Adjust cursor.
        self.cursor = end;

        // Even if all bytes were not written we return len.
        Ok(len)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

impl FakeTerminal {
    pub fn new(w: u16, h: u16) -> Self {
        Self { w, h, buffer: Arc::new(Mutex::new(vec![32; (w * h).into()])), cursor: 0 }
    }

    pub fn screen_without_help_footer(&self) -> String {
        let inner = self.buffer.lock().unwrap();

        // Ignore last two lines.
        inner.chunks(self.w as usize).take(self.h as usize - 2).fold(
            String::new(),
            |mut acc, screen_line| {
                acc.push_str(&format!("{}\n", String::from_utf8_lossy(screen_line)));
                acc
            },
        )
    }
}

impl Terminal for FakeTerminal {
    fn terminal_size(&self) -> (u16, u16) {
        (self.w, self.h)
    }

    fn clear(&mut self) {
        self.cursor = 0;
        // 32 is ascii for space.
        let mut inner = self.buffer.lock().expect("Unable to get lock to buffer.");
        let _ = std::mem::replace(&mut *inner, vec![32; (self.w * self.h).into()]);
    }

    fn goto(&mut self, c: u16, r: u16) {
        assert!(c <= self.w);
        assert!(r <= self.h);

        // (r,c) uses (1, 1) based indexing due to termion.
        self.cursor = ((r - 1) * self.w + (c - 1)) as usize;
    }

    // set_red_color, reset_color, switch_interactive and switch_normal are nops for FakeTerminal
    fn set_red_color(&mut self) {}
    fn reset_color(&mut self) {}
    fn switch_interactive(&mut self) {}
    fn switch_normal(&mut self) {}
}

pub fn get_test_lines() -> Vec<Line> {
    vec![
        "{",
        "A long line of length which will overflow screen",
        "Short line2",
        "", // Empty line
        "Another long line which will overflow screen",
        "Second Last line",
        "The last line is also long.",
        "}",
    ]
    .into_iter()
    .map(|s| Line::new(s.to_string()))
    .collect()
}
