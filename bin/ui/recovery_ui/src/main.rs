// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate failure;
extern crate fidl_fuchsia_amber as amber;
extern crate font_rs;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_framebuffer;
extern crate fuchsia_zircon as zx;

mod text;

use async::futures::FutureExt;
use failure::Fail;
use fuchsia_framebuffer::{Config, Frame, FrameBuffer, PixelFormat};
use std::cell::RefCell;
use std::io::{self, Read};
use std::rc::Rc;
use std::thread;
use text::Face;

static FONT_DATA: &'static [u8] =
    include_bytes!("../../../fonts/third_party/robotoslab/RobotoSlab-Regular.ttf");

/// Convenience function that can be called from main and causes the Fuchsia process being
/// run over ssh to be terminated when the user hits control-C.
fn wait_for_close() {
    thread::spawn(move || loop {
        let mut input = [0; 1];
        if io::stdin().read_exact(&mut input).is_err() {
            std::process::exit(0);
        }
    });
}

struct RecoveryUI<'a> {
    face: Face<'a>,
    frame: Frame,
    config: Config,
    text_size: u32,
}

impl<'a> RecoveryUI<'a> {
    fn draw(&mut self, url: &str, user_code: &str) {
        let values565 = &[31, 248];
        let values8888 = &[255, 0, 255, 255];

        for y in 0..self.config.height {
            for x in 0..self.config.width {
                match self.config.format {
                    PixelFormat::RgbX888 => self.frame.write_pixel(x, y, values8888),
                    PixelFormat::Argb8888 => self.frame.write_pixel(x, y, values8888),
                    PixelFormat::Rgb565 => self.frame.write_pixel(x, y, values565),
                    _ => {}
                }
            }
        }

        let (width, height) = self.face.measure_text(self.text_size, url);

        self.face.draw_text_at(
            &mut self.frame,
            (self.config.width / 2) as i32 - width / 2,
            (self.config.height / 4) as i32 + height / 2,
            self.text_size,
            url,
        );

        let (width, height) = self.face.measure_text(self.text_size, user_code);

        self.face.draw_text_at(
            &mut self.frame,
            (self.config.width / 2) as i32 - width / 2,
            (self.config.height / 2 + self.config.height / 4) as i32 + height / 2,
            self.text_size,
            user_code,
        );
    }
}

fn main() {
    println!("Recovery UI");
    wait_for_close();

    let face = Face::new(FONT_DATA).unwrap();

    let mut executor = async::Executor::new().unwrap();

    let fb = FrameBuffer::new(None, &mut executor).unwrap();
    let config = fb.get_config();

    let frame = fb.new_frame(&mut executor).unwrap();
    frame.present(&fb).unwrap();

    let mut ui = RecoveryUI {
        face,
        frame,
        config,
        text_size: config.height / 12,
    };

    ui.draw("Verification URL", "User Code");

    let ui_login = Rc::new(RefCell::new(ui));
    let ui_fail = ui_login.clone();

    let amber_control = app::client::connect_to_service::<amber::ControlMarker>().unwrap();

    let list_srcs = amber_control
        .list_srcs()
        .map_err(|e| e.context("listlist_srcs failed"))
        .map(move |src_list| {
            if src_list.len() > 0 {
                let login = amber_control
                    .login(&src_list[0].id)
                    .map_err(|e| e.context("login failed"))
                    .map(move |device_code| println!("device_code = {:#?}", device_code));
                async::spawn_local(login.recover(move |err| {
                    println!("in login recover {:#?}", err);
                    let mut ui_local = ui_login.borrow_mut();
                    ui_local.draw(&src_list[0].id, "Login failed")
                }));
            } else {
                ui_fail
                    .borrow_mut()
                    .draw("Could not get", "source list from Amber")
            }
        });

    async::spawn_local(list_srcs.recover(|err| println!("in list_srcs recover {:#?}", err)));

    loop {
        let timeout = async::Timer::<()>::new(zx::Time::INFINITE);
        executor.run_singlethreaded(timeout).unwrap();
        println!("tick");
    }
}
