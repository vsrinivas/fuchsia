// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    std::io::Write,
    termion::{clear, color, cursor},
};

pub trait Terminal: Write {
    // Get supported terminal size.
    fn terminal_size(&self) -> (u16, u16);

    // Clears the terminal.
    fn clear(&mut self);

    // Move to (c,r) in terminal
    fn goto(&mut self, c: u16, r: u16);

    // Set terminal color to red for next output.
    fn set_red_color(&mut self);

    // Reset terminal color for next output.
    fn reset_color(&mut self);

    // Switches the terminal to interactive mode.
    // Restore cursor, hide cursor, clear screen.
    fn switch_interactive(&mut self);

    // Restore original terminal after interactive mode.
    // Restore cursor, show cursor, clear screen.
    fn switch_normal(&mut self);
}

/// Represents a terminal managed using termion.
pub struct Termion<W: Write> {
    stdout: W,
}

impl<W: Write> Termion<W> {
    pub fn new(stdout: W) -> Self {
        Self { stdout }
    }
}

impl<W: Write> Terminal for Termion<W> {
    fn terminal_size(&self) -> (u16, u16) {
        termion::terminal_size().expect("Unable to get terminal size using termion.")
    }

    fn clear(&mut self) {
        let (w, h) = self.terminal_size();
        for i in 0..h {
            self.goto(1, (i + 1) as u16);
            write!(self.stdout, "{:width$}", ' ', width = w as usize)
                .expect("Unable to write to terminal.");
        }
    }

    fn goto(&mut self, c: u16, r: u16) {
        write!(self.stdout, "{}", termion::cursor::Goto(c, r))
            .expect("Unable to change cursor position.");
    }

    fn set_red_color(&mut self) {
        write!(self.stdout, "{}", color::Fg(color::Red))
            .expect("Unable to set terminal color to red.");
    }

    fn reset_color(&mut self) {
        write!(self.stdout, "{}", color::Fg(color::Reset))
            .expect("Unable to reset terminal color.");
    }

    fn switch_interactive(&mut self) {
        write!(self.stdout, "{}{}{}", cursor::Restore, cursor::Hide, clear::All)
            .expect("Unable to switch to interactive mode.");
        self.stdout.flush().expect("Unable to flush to stdout.");
    }

    fn switch_normal(&mut self) {
        write!(self.stdout, "{}{}{}", cursor::Restore, cursor::Show, clear::All)
            .expect("Unable to restore back terminal.");
        self.stdout.flush().expect("Unable to flush to stdout.");
    }
}

impl<W: Write> Write for Termion<W> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.stdout.write(buf)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        self.stdout.flush()
    }
}
