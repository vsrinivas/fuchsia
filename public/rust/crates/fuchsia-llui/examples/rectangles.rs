// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "1024"]

#[macro_use]
extern crate error_chain;
extern crate fuchsia_llui as llui;

use llui::{Color, FrameBuffer, wait_for_close};
use std::{thread, time};

error_chain!{
    links {
        LUI(::llui::Error, ::llui::ErrorKind);
    }
}

#[derive(Copy, Clone, Debug, PartialEq, PartialOrd)]
pub struct Size {
    pub width: i32,
    pub height: i32,
}

impl Size {
    pub fn add(&self, size: Size) -> Size {
        Size {
            width: self.width + size.width,
            height: self.height + size.height,
        }
    }

    pub fn subtract(&self, size: Size) -> Size {
        Size {
            width: self.width - size.width,
            height: self.height - size.height,
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, PartialOrd)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}

impl Point {
    pub fn add(&self, pt: Point) -> Point {
        Point {
            x: self.x + pt.x,
            y: self.y + pt.y,
        }
    }

    pub fn subtract(&self, pt: Point) -> Point {
        Point {
            x: self.x - pt.x,
            y: self.y - pt.y,
        }
    }

    pub fn to_size(&self) -> Size {
        Size {
            width: self.x,
            height: self.y,
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, PartialOrd)]
pub struct Rectangle {
    pub origin: Point,
    pub size: Size,
}

impl Rectangle {
    pub fn empty(&self) -> bool {
        self.size.width <= 0 && self.size.height <= 0
    }

    pub fn bottom(&self) -> i32 {
        self.origin.y + self.size.height
    }
}

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

fn fill_rectangle(fb: &mut FrameBuffer, color: &Color, r: &Rectangle) {
    let pixel_size = fb.get_pixel_size();
    let stride = fb.get_stride();
    let stride_bytes = stride * pixel_size;
    let values565 = color.to_565();
    let values8888 = color.to_8888();
    let pixel_data = fb.get_pixels();
    for y in r.origin.y..r.bottom() {
        let row_offset = stride_bytes * y as usize;
        let left_offset = row_offset + r.origin.x as usize * pixel_size;
        let right_offset = left_offset + r.size.width as usize * pixel_size;
        let row_slice = &mut pixel_data[left_offset..right_offset];
        for pixel_slice in row_slice.chunks_mut(pixel_size) {
            if pixel_size == 4 {
                pixel_slice.copy_from_slice(&values8888);
            } else {
                pixel_slice.copy_from_slice(&values565);
            }
        }
    }
}

fn run() -> Result<()> {
    wait_for_close();

    let mut fb = FrameBuffer::new(None)?;
    println!("fb = {:?}", fb);
    let c1 = Color::from_hash_code("#D0D0D0");
    let c2 = Color::from_hash_code("#FFCC66");
    let c3 = Color::from_hash_code("#00FFFF");
    let fuchsia = Color::from_hash_code("#FF00FF");
    let r1 = Rectangle {
        origin: Point { x: 200, y: 200 },
        size: Size {
            width: 200,
            height: 200,
        },
    };
    let r2 = Rectangle {
        origin: Point { x: 500, y: 100 },
        size: Size {
            width: 100,
            height: 100,
        },
    };
    let r3 = Rectangle {
        origin: Point { x: 300, y: 500 },
        size: Size {
            width: 300,
            height: 100,
        },
    };
    let mut i: usize = 0;
    loop {
        fill_with_color(&mut fb, &fuchsia);
        match i % 3 {
            0 => {
                fill_rectangle(&mut fb, &c1, &r1);
                fill_rectangle(&mut fb, &c2, &r2);
                fill_rectangle(&mut fb, &c3, &r3);
            }
            1 => {
                fill_rectangle(&mut fb, &c2, &r1);
                fill_rectangle(&mut fb, &c3, &r2);
                fill_rectangle(&mut fb, &c1, &r3);
            }
            _ => {
                fill_rectangle(&mut fb, &c3, &r1);
                fill_rectangle(&mut fb, &c1, &r2);
                fill_rectangle(&mut fb, &c2, &r3);
            }
        }
        i = i.wrapping_add(1);
        fb.flush().unwrap();
        thread::sleep(time::Duration::from_millis(800));
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
