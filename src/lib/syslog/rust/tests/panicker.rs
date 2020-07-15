// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_syslog as syslog;
use std::panic;
use std::process;
use std::thread;

fn main() {
    let orig_hook = panic::take_hook();
    panic::set_hook(Box::new(move |panic_info| {
        // invoke the default handler and exit the process
        orig_hook(panic_info);
        // panic is the expected behaviour here, so we're OK
        process::exit(0);
    }));

    syslog::init_with_tags(&["panicker"]).expect("should not fail");

    let _ = thread::spawn(move || {
        panic!("oh no, I panicked");
    })
    .join();
}
