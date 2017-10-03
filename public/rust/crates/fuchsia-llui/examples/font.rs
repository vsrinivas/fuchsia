// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "1024"]

#[macro_use]
extern crate error_chain;
extern crate fuchsia_llui as llui;
extern crate font_rs;

use font_rs::font::{GlyphBitmap, parse};
use llui::{Color, FrameBuffer, wait_for_close};
use std::{thread, time};

error_chain!{
    links {
        LUI(::llui::Error, ::llui::ErrorKind);
    }
}

static FONT_DATA: &'static [u8] =
    include_bytes!("../../../../../bin/fonts/third_party/robotoslab/RobotoSlab-Regular.ttf");

fn fill_with_color(fb: &mut FrameBuffer, color: &Color) {
    let pixel_size = fb.get_pixel_size();
    let values565 = color.to_565();
    let values8888 = color.to_8888();
    let pixel_data = fb.get_pixels();
    for pixel_slice in pixel_data.chunks_mut(pixel_size) {
        if pixel_size == 4 {
            pixel_slice.copy_from_slice(&values8888);
        } else {
            pixel_slice.copy_from_slice(&values565);
        }
    }
}

fn draw_centered_glyph(fb: &mut FrameBuffer, color: &Color, glyph: &GlyphBitmap) {
    let (width, height) = fb.get_dimensions();
    let center_x = width / 2;
    let center_y = height / 2;
    let top = center_y - glyph.height / 2;
    let left = center_x - glyph.width / 2;
    let pixel_size = fb.get_pixel_size();
    let stride = fb.get_stride();
    let stride_bytes = stride * pixel_size;
    let pixel_data = fb.get_pixels();
    let glyph_data = &glyph.data.as_slice();
    let mut y = top;
    for glyph_row in glyph_data.chunks(glyph.width) {
        let mut x = left;
        let row_offset = stride_bytes * y as usize;
        for one_pixel in glyph_row {
            let left_offset = row_offset + x as usize * pixel_size;
            let scale = f64::from(*one_pixel) / 256.0;
            if *one_pixel > 0 {
                let pixel_slice = &mut pixel_data[left_offset..left_offset + pixel_size];
                let scaled_color = color.scale(scale);
                if pixel_size == 4 {
                    let values8888 = scaled_color.to_8888();
                    pixel_slice.copy_from_slice(&values8888);
                } else {
                    let values565 = scaled_color.to_565();
                    pixel_slice.copy_from_slice(&values565);
                }
            }
            x += 1;
        }
        y += 1;
    }
}

fn run() -> Result<()> {
    wait_for_close();

    let mut fb = FrameBuffer::new(None)?;
    println!("fb = {:?}", fb);
    let black_color = Color::from_hash_code("#000000");
    let c1 = Color::from_hash_code("#FF00FF");
    let font = parse(FONT_DATA).unwrap();
    let (_, height) = fb.get_dimensions();
    let mut i: u16 = 0;
    loop {
        let glyph = font.render_glyph(35 + i % 26, height as u32).unwrap();
        fill_with_color(&mut fb, &black_color);
        draw_centered_glyph(&mut fb, &c1, &glyph);
        fb.flush().unwrap();
        thread::sleep(time::Duration::from_millis(300));
        i = i.wrapping_add(1);
    }
}

fn main() {
    if let Err(ref e) = run() {
        println!("error: {}", e);

        for e in e.iter().skip(1) {
            println!("caused by: {}", e);
        }

        // The backtrace is not always generated. Try to run this example
        // with `RUST_BACKTRACE=1`.
        if let Some(backtrace) = e.backtrace() {
            println!("backtrace: {:?}", backtrace);
        }

        ::std::process::exit(1);
    }
}
