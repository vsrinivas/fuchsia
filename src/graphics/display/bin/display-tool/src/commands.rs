// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Result},
    display_utils::{
        Controller, DisplayConfig, DisplayId, Image, Layer, LayerConfig, PixelFormat, VsyncEvent,
    },
    futures::StreamExt,
    std::io::Write,
};

use crate::{draw, fps::Counter};

const CLEAR: &str = "\x1B[2K\r";

pub(crate) fn show_display_info(
    controller: &Controller,
    id: Option<u64>,
    fidl: bool,
) -> Result<()> {
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

pub(crate) async fn fps(controller: &Controller, id: Option<u64>) -> Result<()> {
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

    // The display driver does not send vsync events to a client unless it successfully applies a
    // display configuration. Build a single full screen layer with a solid color to generate
    // hardware vsync events.
    // NOTE: An empty config results in artificial vsync events to be generated (notably in the
    // Intel driver). The config must contain at least one primary layer to obtain an accurate
    // hardware sample.

    // Obtain the display resolution based on the display's preferred mode.
    let (width, height) = {
        let mode = display.0.modes[0];
        (mode.horizontal_resolution, mode.vertical_resolution)
    };
    let params = display_utils::ImageParameters {
        width,
        height,
        pixel_format: PixelFormat::Argb8888,
        color_space: fidl_fuchsia_sysmem::ColorSpaceType::Srgb,
        name: Some("display-tool fps layer".to_string()),
    };
    let image = Image::create(controller.clone(), &params).await?;
    // Fill with Fuchsia as the color ([blue, green, red, alpha])
    draw::fill(&image, &[255, 0, 255, 255]).context("failed to draw fill color")?;

    // Ensure that vsync events are enabled before we issue the first call to ApplyConfig.
    let mut vsync = controller.add_vsync_listener(id.map(DisplayId))?;

    let layer = controller.create_layer().await?;
    let configs = vec![DisplayConfig {
        id: display.id(),
        layers: vec![Layer {
            id: layer,
            config: LayerConfig::Primary {
                image_id: image.id,
                image_config: params.into(),
                unblock_event: None,
                retirement_event: None,
            },
        }],
    }];
    controller.apply_config(&configs).await?;

    // Start sampling vsync frequency.
    let mut counter = Counter::new();
    while let Some(VsyncEvent { id, timestamp, .. }) = vsync.next().await {
        counter.add(timestamp);
        let stats = counter.stats();

        print!(
            "{}Display {} refresh rate {:.2} Hz ({:.5} ms)",
            CLEAR, id.0, stats.sample_rate_hz, stats.sample_time_delta_ms
        );
        std::io::stdout().flush()?;
    }

    Err(format_err!("stopped receiving vsync events"))
}
