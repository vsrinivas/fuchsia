// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::Write;

pub struct MockWriter {
    data: String,
}

impl MockWriter {
    pub fn new() -> Self {
        MockWriter { data: "".to_string() }
    }
    pub fn get_data(&self) -> String {
        return format!("\n{}", self.data);
    }
}

impl Write for MockWriter {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.data = format!(
            "{}{}",
            self.data,
            std::str::from_utf8(buf).unwrap_or("Cannot convert u8 to String.")
        );
        return Ok(buf.len());
    }
    fn flush(&mut self) -> std::io::Result<()> {
        return Ok(());
    }
}
