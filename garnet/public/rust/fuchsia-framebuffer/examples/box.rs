// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fuchsia_async::{self as fasync, DurationExt, Timer};
use fuchsia_framebuffer::{
    to_565, Config, Frame, FrameBuffer, FrameSet, FrameUsage, ImageId, PixelFormat, VSyncMessage,
};
use fuchsia_zircon::DurationNum;
use futures::{channel::mpsc::unbounded, future, StreamExt, TryFutureExt};
use std::{
    cell::RefCell,
    collections::BTreeSet,
    env,
    io::{self, Read},
    rc::Rc,
    thread,
    time::Instant,
};

/// Convenience function that can be called from main and causes the Fuchsia process being
/// run over ssh to be terminated when the user hits control-C.
pub fn wait_for_close() {
    if let Some(argument) = env::args().next() {
        if !argument.starts_with("/tmp") {
            return;
        }
    }

    thread::spawn(move || loop {
        let mut input = [0; 1];
        match io::stdin().read_exact(&mut input) {
            Ok(()) => {}
            Err(_) => {
                std::process::exit(0);
            }
        }
    });
}

struct FrameManager {
    frame_set: FrameSet,
}

// this buffering approach will only work with two images, since
// as soon as it is released by the display controller it is prepared
// for use again and their can only be one prepared image.
const BUFFER_COUNT: usize = 2;

impl FrameManager {
    pub async fn new(fb: &mut FrameBuffer) -> Result<FrameManager, Error> {
        // this buffering approach will only work with two images, since
        // as soon as it is released by the display controller it is prepared
        // for use again and their can only be one prepared image.
        let mut available = BTreeSet::new();
        let config = fb.get_config();
        let image_ids = fb.get_image_ids();
        for image_id in image_ids {
            let frame = fb.get_frame_mut(image_id);
            let black = [0x00, 0x00, 0x00, 0xFF];
            if config.format != PixelFormat::Rgb565 {
                frame.fill_rectangle(0, 0, config.width, config.height, &black);
            } else {
                frame.fill_rectangle(0, 0, config.width, config.height, &to_565(&black));
            }
            available.insert(image_id);
        }
        let frame_set = FrameSet::new(available);
        Ok(FrameManager { frame_set })
    }

    pub fn present_prepared(
        &mut self,
        fb: &mut FrameBuffer,
        sender: Option<futures::channel::mpsc::UnboundedSender<ImageId>>,
    ) -> Result<bool, Error> {
        if let Some(prepared) = self.frame_set.prepared {
            fb.present_frame(prepared, sender, true)?;
            self.frame_set.mark_presented(prepared);
            Ok(true)
        } else {
            Ok(false)
        }
    }

    pub fn prepare_frame<F>(&mut self, fb: &mut FrameBuffer, f: F)
    where
        F: FnOnce(&mut Frame),
    {
        if let Some(image_id) = self.frame_set.get_available_image() {
            let frame = fb.get_frame_mut(image_id);
            f(frame);
            self.frame_set.mark_prepared(image_id);
        } else {
            println!("no free image to prepare");
        }
    }

    pub fn frame_done_presenting(&mut self, image_id: ImageId) {
        self.frame_set.mark_done_presenting(image_id);
    }
}

const FRAME_DELTA: u64 = 10_000_000;

fn update(config: &Config, frame: &mut Frame, timestamp: u64) -> Result<(), Error> {
    let box_color = [0x80, 0x00, 0x80, 0xFF];
    let white = [0xFF, 0xFF, 0xFF, 0xFF];
    let box_size = 500;
    let box_x = config.width / 2 - box_size / 2;
    let box_y = config.height / 2 - box_size / 2;
    if config.format != PixelFormat::Rgb565 {
        frame.fill_rectangle(box_x, box_y, box_size, box_size, &box_color);
    } else {
        frame.fill_rectangle(box_x, box_y, box_size, box_size, &to_565(&box_color));
    }
    let delta = ((timestamp / FRAME_DELTA) % box_size as u64) as u32;
    let x = box_x + delta;
    let y = box_y + delta;
    if config.format != PixelFormat::Rgb565 {
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

    // this method is a hack that causes the application to terminate when
    // control-c is called and it has been run via scp/ssh.
    wait_for_close();

    let mut executor = fasync::Executor::new().context("Failed to create executor")?;

    executor.run_singlethreaded(async {
        // create async channel sender/receiver pair to receive vsync messages
        let (sender, mut receiver) = unbounded::<VSyncMessage>();

        // create a framebuffer
        let mut fb = FrameBuffer::new(FrameUsage::Cpu, None, Some(sender)).await?;

        fb.allocate_frames(BUFFER_COUNT, PixelFormat::Argb8888).await?;

        // Find out the details of the display this frame buffer targets
        let config = fb.get_config();

        // Create our sample frame manager, which will create and manage the frames
        // and the display controller images they use.
        let frame_manager = Rc::new(RefCell::new(FrameManager::new(&mut fb).await?));

        // prepare the first frame
        frame_manager.borrow_mut().prepare_frame(&mut fb, |frame| {
            update(&config, frame, 0).expect("update to work");
        });

        // create an async channel sender/receiver pair to receive messages when
        // the image managed by a frame is no longer being displayed.
        let (image_sender, mut image_receiver) = unbounded::<u64>();

        // Present the first image. Without this present, the display controller
        // will not send vsync messages.
        frame_manager
            .borrow_mut()
            .present_prepared(&mut fb, Some(image_sender.clone()))
            .expect("present to work");

        // Prepare a second image. There always wants to be a prepared image
        // to present at a fixed time after vsync.
        frame_manager.borrow_mut().prepare_frame(&mut fb, |frame| {
            update(&config, frame, 0).expect("update to work");
        });

        // keep track of the start time to use to animate the horizontal and
        // vertical lines.
        let start_time = Instant::now();

        // Create a clone of the Rc holding the frame manager to move into
        // the async block that receives messages about images being no longer
        // in use.
        let frame_manager_image = frame_manager.clone();

        let fb_ptr = Rc::new(RefCell::new(fb));
        let fb_ptr2 = fb_ptr.clone();

        // wait for events from the image freed fence to know when an
        // image can prepared.
        fasync::spawn_local(
            async move {
                while let Some(image_id) = image_receiver.next().await {
                    // Grab a mutable reference. This is guaranteed to work
                    // since only one of this closure or the vsync closure can
                    // be in scope at once.
                    let mut frame_manager = frame_manager_image.borrow_mut();

                    // Note the freed image and immediately prepare it. Since there
                    // are only two frames we are guaranteed that at most one can
                    // be free at one time.
                    frame_manager.frame_done_presenting(image_id);

                    // Use elapsed time to animate the horizontal and vertical
                    // lines
                    let time = Instant::now().duration_since(start_time).as_nanos() as u64;
                    let mut fb = fb_ptr.borrow_mut();
                    frame_manager.prepare_frame(&mut fb, |frame| {
                        update(&config, frame, time).expect("update to work");
                    });
                }
                Ok(())
            }
            .unwrap_or_else(|e: failure::Error| {
                println!("error {:#?}", e);
            }),
        );

        // Listen for vsync messages to schedule an update of the displayed image
        fasync::spawn_local(
            async move {
                while let Some(_vsync_message) = receiver.next().await {
                    // Wait an arbitrary 10 milliseconds after vsync to present the
                    // next prepared image.
                    Timer::new(10_i64.millis().after_now()).await;

                    // Grab a mutable reference. This is guaranteed to work
                    // since only one of this closure or the vsync closure can
                    // be in scope at once.
                    let mut frame_manager = frame_manager.borrow_mut();

                    let mut fb = fb_ptr2.borrow_mut();

                    // Present the previously prepared image. As a side effect,
                    // the currently presented image will be eventually freed.
                    frame_manager
                        .present_prepared(&mut fb, Some(image_sender.clone()))
                        .expect("FrameManager::present_prepared to work");
                }
                Ok(())
            }
            .unwrap_or_else(|e: failure::Error| {
                println!("error {:#?}", e);
            }),
        );
        Ok::<(), Error>(())
    })?;
    executor.run_singlethreaded(future::pending::<()>());
    unreachable!();
}
