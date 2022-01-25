// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use display_utils::Controller;

pub(crate) fn show_display_info(controller: &Controller, id: Option<u64>, fidl: bool) {
    let displays = controller.displays();
    println!("{} display(s) available", displays.len());
    for display in displays.iter().filter(|&info| id.map_or(true, |id| info.0.id == id)) {
        if fidl {
            println!("{:#?}", display.0);
        } else {
            println!("{}", display);
        }
    }
}
