// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use failure::{Error, ResultExt};
use fuchsia_async as fasync;
use fuchsia_framebuffer::{Config, Frame, FrameBuffer, PixelFormat, VSyncMessage};
use futures::{channel::mpsc::unbounded, future, StreamExt, TryFutureExt};

fn to_565(pixel: &[u8; 4]) -> [u8; 2] {
    let red = pixel[0] >> 3;
    let green = pixel[1] >> 2;
    let blue = pixel[2] >> 3;
    let b1 = (red << 3) | ((green & 0b11_1000) >> 3);
    let b2 = ((green & 0b111) << 5) | blue;
    [b2, b1]
}

const FRAME_DELTA: u64 = 10000000;

fn update(config: &Config, frame: &mut Frame, timestamp: u64) -> Result<(), Error> {
    let box_color = [0x80, 0x00, 0x80, 0xFF];
    let white = [0xFF, 0xFF, 0xFF, 0xFF];
    let box_size = 300;
    let box_x = config.width / 2 - box_size / 2;
    let box_y = config.height / 2 - box_size / 2;
    if config.format == PixelFormat::Argb8888 {
        frame.fill_rectangle(box_x, box_y, box_size, box_size, &box_color);
    } else {
        frame.fill_rectangle(box_x, box_y, box_size, box_size, &to_565(&box_color));
    }
    let delta = ((timestamp / FRAME_DELTA) % box_size as u64) as u32;
    let x = box_x + delta;
    let y = box_y + delta;
    if config.format == PixelFormat::Argb8888 {
        frame.fill_rectangle(x, box_y, 1, box_size, &white);
        frame.fill_rectangle(box_x, y, box_size, 1, &white);
    } else {
        frame.fill_rectangle(x, box_y, 1, box_size, &to_565(&white));
        frame.fill_rectangle(box_x, y, box_size, 1, &to_565(&white));
    }
    Ok(())
}

fn main() -> Result<(), Error> {
    println!("box: started");
    let mut executor = fasync::Executor::new().context("Failed to create executor")?;
    let (sender, mut receiver) = unbounded::<VSyncMessage>();
    let fb = FrameBuffer::new(None, &mut executor, Some(sender))
        .context("Failed to create framebuffer")?;
    let config = fb.get_config();

    let mut frame = fb.new_frame(&mut executor)?;
    let black = [0x00, 0x00, 0x00, 0xFF];
    if config.format == PixelFormat::Argb8888 {
        frame.fill_rectangle(0, 0, config.width, config.height, &black);
    } else {
        frame.fill_rectangle(0, 0, config.width, config.height, &to_565(&black));
    }
    update(&config, &mut frame, 0)?;
    frame.present(&fb)?;

    let mut next_frame = 0;

    fasync::spawn(
        async move {
            while let Some(vsync_message) = receiver.next().await {
                if vsync_message.timestamp > next_frame {
                    next_frame = vsync_message.timestamp + FRAME_DELTA;
                    update(&config, &mut frame, vsync_message.timestamp)?;
                }
            }
            Ok(())
        }
            .unwrap_or_else(|e: failure::Error| {
                println!("error {:#?}", e);
            }),
    );

    executor.run_singlethreaded(future::pending::<()>());
    unreachable!();
}
