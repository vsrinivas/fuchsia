// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fuchsia_syslog as syslog;
use std::io::{BufWriter, Write};

pub struct Printer<T: Write> {
    writer: BufWriter<T>,
}

impl<T> Printer<T>
where
    T: Write,
{
    pub fn new(output: T) -> Printer<T> {
        Printer { writer: BufWriter::new(output) }
    }

    pub fn println(&mut self, contents: String) {
        if let Err(e) = self.writer.write_all(contents.as_bytes()) {
            syslog::fx_log_err!("Error writing to buffer: {}", e);
            return;
        }

        if let Err(e) = self.writer.write_all("\n".as_bytes()) {
            syslog::fx_log_err!("Error writing to buffer: {}", e);
        }
    }

    // Consumes `BufWriter` and returns the underlying buffer.
    // Note that we never call `into_inner()` for the `io::stdout()` variant; so we need to
    // suppress rustc's `dead_code` lint warning here.
    pub fn into_inner(self) -> Result<T, Error> {
        match self.writer.into_inner() {
            Ok(w) => Ok(w),
            Err(e) => Err(format_err!("Error getting writer: {}", e)),
        }
    }
}
