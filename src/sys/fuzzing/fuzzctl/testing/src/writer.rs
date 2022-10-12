// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_fuzzctl::OutputSink,
    std::cell::RefCell,
    std::env,
    std::fmt::Debug,
    std::fmt::Display,
    std::io::{stdout, Write},
    std::rc::Rc,
};

/// `BufferSink` saves its output in a buffer and can verify it against expected output.
#[derive(Debug)]
pub struct BufferSink {
    data: Rc<RefCell<Vec<u8>>>,
    echo: bool,
}

impl BufferSink {
    ///.Creates a `BufferSink`.
    ///
    /// This object will write into the shared `data` vector. When running tests, users may
    /// optional set the FFX_FUZZ_TEST_ECHO_OUTPUT environment variable, which will cause this
    /// object to copy anything written to it to standard output.
    pub fn new(data: Rc<RefCell<Vec<u8>>>) -> Self {
        let echo = env::var("FFX_FUZZ_TEST_ECHO_OUTPUT").is_ok();
        Self { data, echo }
    }
}

impl Clone for BufferSink {
    fn clone(&self) -> Self {
        Self { data: Rc::clone(&self.data), echo: self.echo }
    }
}

impl OutputSink for BufferSink {
    fn write_all(&self, buf: &[u8]) {
        if self.echo {
            let _ = stdout().write_all(buf);
        }
        let mut data = self.data.borrow_mut();
        data.extend_from_slice(buf);
    }

    fn print<D: Display>(&self, message: D) {
        self.write_all(message.to_string().as_bytes());
    }

    fn error<D: Display>(&self, message: D) {
        self.write_all(message.to_string().as_bytes());
    }
}
