// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ansi_term::Colour::Red;

#[test]
fn stdout_ansi_test() {
    println!("{}", Red.paint("red stdout"));
}

#[test]
fn log_ansi_test() {
    fuchsia_syslog::init().expect("initializing log");
    log::info!("{}", Red.paint("red log"));
}
