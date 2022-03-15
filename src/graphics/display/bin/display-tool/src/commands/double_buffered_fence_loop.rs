// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///! Demonstrates building an animation using a double-buffer swapchain. The swapchain is
///! represented by two images that alternate in their assignment to a primary layer. Writes to each
///! buffer and the resulting swap is synchronized using each configuration's retirement fence,
///! which is aligned to the display's vsync events by the display driver.
use {
    anyhow::{Context, Result},
    display_utils::{
        Controller, DisplayConfig, DisplayInfo, Image, ImageParameters, Layer, LayerConfig,
        PixelFormat,
    },
    std::{borrow::Borrow, cmp::min, io::Write},
};

use crate::{
    draw::{Frame, MappedImage},
    fps::Counter,
};

const CLEAR: &str = "\x1B[2K\r";

struct BouncingSquare {
    color: [u8; 4],
    frame: Frame,
    velocity: (i64, i64),
}

impl BouncingSquare {
    // Update the position along the velocity vector. The velocities are updated such that the
    // square bounces off the boundaries of an enclosing screen of the given dimensions.
    //
    // The velocity is treated as a fixed increment in pixels and the calculation intentionally
    // does not factor in elapsed time or interpolate between steps to make the speed of the boxes
    // vary with the framerate.
    fn update(&mut self, screen_width: u32, screen_height: u32) {
        let x = self.frame.pos_x as i64 + self.velocity.0;
        let y = self.frame.pos_y as i64 + self.velocity.1;
        if x < 0 || x as u32 + self.frame.width > screen_width {
            self.velocity.0 *= -1;
        }
        if y < 0 || y as u32 + self.frame.height > screen_height {
            self.velocity.1 *= -1;
        }
        self.frame.pos_x = min(x.abs() as u32, screen_width - self.frame.width - 1);
        self.frame.pos_y = min(y.abs() as u32, screen_height - self.frame.height - 1);
    }
}

pub async fn run(controller: &Controller, display: &DisplayInfo) -> Result<()> {
    // Obtain the display resolution based on the display's preferred mode.
    let (width, height) = {
        let mode = display.0.modes[0];
        (mode.horizontal_resolution, mode.vertical_resolution)
    };
    let params = ImageParameters {
        width,
        height,
        pixel_format: PixelFormat::Argb8888,
        color_space: fidl_fuchsia_sysmem::ColorSpaceType::Srgb,
        name: Some("display-tool squares layer".to_string()),
    };

    // Construct a single layer and two images. This represents our swapchain.
    let layer = controller.create_layer().await?;
    let images = vec![
        MappedImage::create(Image::create(controller.clone(), &params).await?)?,
        MappedImage::create(Image::create(controller.clone(), &params).await?)?,
    ];
    let retirement_events = vec![controller.create_event()?, controller.create_event()?];

    // Construct squares that start out at the 4 corners of the screen.
    let dim = height / 8;
    let mut squares = vec![
        BouncingSquare {
            color: [255, 100, 0, 255],
            frame: Frame { pos_x: 0, pos_y: 0, width: dim, height: dim },
            velocity: (16, 16),
        },
        BouncingSquare {
            color: [255, 0, 255, 255],
            frame: Frame { pos_x: width - dim - 1, pos_y: 0, width: dim, height: dim },
            velocity: (-8, 8),
        },
        BouncingSquare {
            color: [100, 255, 0, 255],
            frame: Frame { pos_x: 0, pos_y: height - dim - 1, width: dim, height: dim },
            velocity: (4, -8),
        },
        BouncingSquare {
            color: [0, 100, 255, 255],
            frame: Frame {
                pos_x: width - dim - 1,
                pos_y: height - dim - 1,
                width: dim,
                height: dim,
            },
            velocity: (-16, -8),
        },
    ];

    // Apply the first config.
    let mut current_config = 0;
    controller
        .apply_config(&[DisplayConfig {
            id: display.id(),
            layers: vec![Layer {
                id: layer,
                config: LayerConfig::Primary {
                    image_id: images[current_config].id(),
                    image_config: params.borrow().into(),
                    unblock_event: None,
                    retirement_event: Some(retirement_events[current_config].id()),
                },
            }],
        }])
        .await?;

    let mut counter = Counter::new();
    loop {
        // Log the frame rate.
        counter.add(fuchsia_zircon::Time::get_monotonic());
        let stats = counter.stats();
        print!(
            "{}Display {:.2} fps ({:.5} ms)",
            CLEAR, stats.sample_rate_hz, stats.sample_time_delta_ms
        );
        std::io::stdout().flush()?;

        // Record the retirement fence of the current config which we use to synchronize
        // frames.
        let retirement = &retirement_events[current_config];

        // Prepare the next image.
        current_config ^= 1;
        let next_image = &images[current_config];

        // Draw the next frame.
        next_image.zero().context("failed to clear background")?;
        for s in &mut squares {
            next_image.fill_region(&s.color, &s.frame).context("failed to draw bouncing square")?;
            s.update(width, height);
        }
        next_image.cache_clean()?;

        // Request the swap.
        controller
            .apply_config(&[DisplayConfig {
                id: display.id(),
                layers: vec![Layer {
                    id: layer,
                    config: LayerConfig::Primary {
                        image_id: images[current_config].id(),
                        image_config: params.borrow().into(),
                        unblock_event: None,
                        retirement_event: Some(retirement_events[current_config].id()),
                    },
                }],
            }])
            .await?;

        // Wait for the previous frame image to retire before drawing on it.
        retirement.wait().await?;
    }
}
