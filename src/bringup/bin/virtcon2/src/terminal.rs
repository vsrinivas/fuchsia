// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    carnelian::Size,
    std::{cell::RefCell, fs::File, io::prelude::*, rc::Rc},
    term_model::{
        clipboard::Clipboard, config::Config, event::EventListener, grid::Scroll, term::SizeInfo,
        Term,
    },
};

/// Empty type for term model config.
#[derive(Default)]
pub struct TermConfig;
pub type TerminalConfig = Config<TermConfig>;

/// Wrapper around a term model instance and its associated PTY fd.
pub struct Terminal<T> {
    term: Rc<RefCell<Term<T>>>,
    pty_fd: File,
    title: String,
}

impl<T> Terminal<T> {
    pub fn new(event_listener: T, pty_fd: File, title: String) -> Self {
        // Initial size info used before we know what the real size is.
        let cell_size = Size::new(8.0, 16.0);
        let size_info = SizeInfo {
            width: cell_size.width * 80.0,
            height: cell_size.height * 24.0,
            cell_width: cell_size.width,
            cell_height: cell_size.height,
            padding_x: 0.0,
            padding_y: 0.0,
            dpr: 1.0,
        };
        let term = Rc::new(RefCell::new(Term::new(
            &TerminalConfig::default(),
            &size_info,
            Clipboard::new(),
            event_listener,
        )));

        Self { term: Rc::clone(&term), pty_fd, title }
    }

    #[cfg(test)]
    fn new_for_test(event_listener: T, pty_fd: File) -> Self {
        Self::new(event_listener, pty_fd, String::new())
    }

    pub fn clone_term(&self) -> Rc<RefCell<Term<T>>> {
        Rc::clone(&self.term)
    }

    pub fn try_clone_pty_fd(&self) -> Result<File, Error> {
        let fd = self.pty_fd.try_clone()?;
        Ok(fd)
    }

    pub fn try_clone(&self) -> Result<Self, Error> {
        let pty_fd = self.try_clone_pty_fd()?;
        let term = self.clone_term();
        let title = self.title.clone();
        Ok(Self { term, pty_fd, title })
    }

    pub fn resize(&mut self, size_info: &SizeInfo) {
        let mut term = self.term.borrow_mut();
        term.resize(size_info);
    }

    pub fn title(&self) -> &str {
        self.title.as_str()
    }

    pub fn scroll(&mut self, scroll: Scroll)
    where
        T: EventListener,
    {
        let mut term = self.term.borrow_mut();
        term.scroll_display(scroll);
    }
}

impl<T> Write for Terminal<T> {
    fn write(&mut self, buf: &[u8]) -> Result<usize, std::io::Error> {
        self.pty_fd.write(buf)
    }

    fn flush(&mut self) -> Result<(), std::io::Error> {
        self.pty_fd.flush()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        pty::Pty,
        term_model::event::{Event, EventListener},
    };

    #[derive(Default)]
    struct TestListener;

    impl EventListener for TestListener {
        fn send_event(&self, _event: Event) {}
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_create_terminal() -> Result<(), Error> {
        let pty = Pty::new()?;
        let fd = pty.try_clone_fd()?;
        let _ = Terminal::new_for_test(TestListener::default(), fd);
        Ok(())
    }
}
