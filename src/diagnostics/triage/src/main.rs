// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    libtriage::act,
    structopt::StructOpt,
    triage_lib::{app::App, Options},
};

fn main() {
    let app = App::new(Options::from_args());
    match app.run() {
        Ok(results) => results.write_report(&mut std::io::stdout()).unwrap(),
        Err(e) => act::report_failure(e),
    };
}
