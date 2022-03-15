// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    display_utils::Controller,
};

mod double_buffered_fence_loop;
mod static_config_vsync_loop;

pub fn show_display_info(controller: &Controller, id: Option<u64>, fidl: bool) -> Result<()> {
    let displays = controller.displays();
    println!("{} display(s) available", displays.len());
    for display in displays.iter().filter(|&info| id.map_or(true, |id| info.0.id == id)) {
        if fidl {
            println!("{:#?}", display.0);
        } else {
            println!("{}", display);
        }
    }
    Ok(())
}

pub async fn vsync(controller: &Controller, id: Option<u64>) -> Result<()> {
    let displays = controller.displays();
    if displays.is_empty() {
        return Err(format_err!("no displays found"));
    }

    let display = match id {
        // Pick the first available display if no ID was specified.
        None => &displays[0],
        Some(id) => displays
            .iter()
            .find(|d| d.id().0 == id)
            .ok_or_else(|| format_err!("display with id '{}' not found", id))?,
    };

    static_config_vsync_loop::run(controller, display).await
}

pub async fn squares(controller: &Controller, id: Option<u64>) -> Result<()> {
    let displays = controller.displays();
    if displays.is_empty() {
        return Err(format_err!("no displays found"));
    }

    let display = match id {
        // Pick the first available display if no ID was specified.
        None => &displays[0],
        Some(id) => displays
            .iter()
            .find(|d| d.id().0 == id)
            .ok_or_else(|| format_err!("display with id '{}' not found", id))?,
    };

    double_buffered_fence_loop::run(controller, display).await
}
