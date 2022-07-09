// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/104019): Consider enabling globally.
#![deny(unused_crate_dependencies)]

pub mod descriptor;
pub mod fragment_io;
mod lossy_text;
mod reassembler;
pub mod report_skipped;
pub mod run;

#[cfg(test)]
mod test_util;
