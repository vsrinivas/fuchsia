// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fuchsia_async::{self as fasync, DurationExt, Timer};
use fuchsia_framebuffer::{Config, Frame, FrameBuffer, PixelFormat, VSyncMessage};
use fuchsia_zircon::DurationNum;
use futures::{channel::mpsc::unbounded, future, StreamExt, TryFutureExt};
use std::{
    cell::RefCell,
    collections::{BTreeMap, BTreeSet},
    env,
    io::{self, Read},
    rc::Rc,
    thread,
    time::Instant,
};

#[cfg(test)]
use std::ops::Range;

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

fn to_565(pixel: &[u8; 4]) -> [u8; 2] {
    let red = pixel[0] >> 3;
    let green = pixel[1] >> 2;
    let blue = pixel[2] >> 3;
    let b1 = (red << 3) | ((green & 0b11_1000) >> 3);
    let b2 = ((green & 0b111) << 5) | blue;
    [b2, b1]
}

type ImageId = u64;

struct FrameSet {
    available: BTreeSet<ImageId>,
    prepared: Option<ImageId>,
    presented: BTreeSet<ImageId>,
}

impl FrameSet {
    pub fn new(available: BTreeSet<ImageId>) -> FrameSet {
        FrameSet { available, prepared: None, presented: BTreeSet::new() }
    }

    #[cfg(test)]
    pub fn new_with_range(r: Range<ImageId>) -> FrameSet {
        let mut available = BTreeSet::new();
        for image_id in r {
            available.insert(image_id);
        }
        Self::new(available)
    }

    pub fn mark_presented(&mut self, image_id: ImageId) {
        assert!(
            !self.presented.contains(&image_id),
            "Attempted to mark as presented image {} which was already in the presented image set",
            image_id
        );
        self.presented.insert(image_id);
        self.prepared = None;
    }

    pub fn mark_done_presenting(&mut self, image_id: ImageId) {
        assert!(
            self.presented.remove(&image_id),
            "Attempted to mark as freed image {} which was not the presented image",
            image_id
        );
        self.available.insert(image_id);
    }

    pub fn mark_prepared(&mut self, image_id: ImageId) {
        assert!(self.prepared.is_none(), "Trying to mark image {} as prepared when image {} is prepared and has not been presented", image_id, self.prepared.unwrap());
        self.prepared.replace(image_id);
        self.available.remove(&image_id);
    }

    pub fn get_available_image(&mut self) -> Option<ImageId> {
        let first = self.available.iter().next().map(|a| *a);
        if let Some(first) = first {
            self.available.remove(&first);
        }
        first
    }
}

#[cfg(test)]
mod frameset_tests {
    use crate::{FrameSet, ImageId};
    use std::ops::Range;

    const IMAGE_RANGE: Range<ImageId> = 200..202;

    #[test]
    #[should_panic]
    fn test_double_prepare() {
        let mut fs = FrameSet::new_with_range(IMAGE_RANGE);

        fs.mark_prepared(100);
        fs.mark_prepared(200);
    }

    #[test]
    #[should_panic]
    fn test_not_presented() {
        let mut fs = FrameSet::new_with_range(IMAGE_RANGE);
        fs.mark_done_presenting(100);
    }

    #[test]
    #[should_panic]
    fn test_already_presented() {
        let mut fs = FrameSet::new_with_range(IMAGE_RANGE);
        fs.mark_presented(100);
        fs.mark_presented(100);
    }

    #[test]
    fn test_basic_use() {
        let mut fs = FrameSet::new_with_range(IMAGE_RANGE);
        let avail = fs.get_available_image();
        assert!(avail.is_some());
        let avail = avail.unwrap();
        assert!(!fs.available.contains(&avail));
        assert!(!fs.presented.contains(&avail));
        fs.mark_prepared(avail);
        assert_eq!(fs.prepared.unwrap(), avail);
        fs.mark_presented(avail);
        assert!(fs.prepared.is_none());
        assert!(!fs.available.contains(&avail));
        assert!(fs.presented.contains(&avail));
        fs.mark_done_presenting(avail);
        assert!(fs.available.contains(&avail));
        assert!(!fs.presented.contains(&avail));
    }
}

struct FrameManager {
    frames: BTreeMap<ImageId, Frame>,
    frame_set: FrameSet,
}

impl FrameManager {
    pub async fn new(fb: &mut FrameBuffer) -> Result<FrameManager, Error> {
        // this buffering approach will only work with two images, since
        // as soon as it is released by the display controller it is prepared
        // for use again and their can only be one prepared image.
        const BUFFER_COUNT: usize = 2; // this buffering approach
        let mut frames = BTreeMap::new();
        let mut available = BTreeSet::new();
        let config = fb.get_config();
        for _ in 0..BUFFER_COUNT {
            let mut frame = Frame::new(fb).await?;
            let black = [0x00, 0x00, 0x00, 0xFF];
            if config.format == PixelFormat::Argb8888 {
                frame.fill_rectangle(0, 0, config.width, config.height, &black);
            } else {
                frame.fill_rectangle(0, 0, config.width, config.height, &to_565(&black));
            }
            available.insert(frame.image_id);
            frames.insert(frame.image_id, frame);
        }
        let frame_set = FrameSet::new(available);
        Ok(FrameManager { frames, frame_set })
    }

    pub fn present_prepared(
        &mut self,
        fb: &mut FrameBuffer,
        sender: Option<futures::channel::mpsc::UnboundedSender<ImageId>>,
    ) -> Result<bool, Error> {
        if let Some(prepared) = self.frame_set.prepared {
            let frame = self.frames.get(&prepared).expect("prepared frame to be in frame map");
            frame.present(fb, sender)?;
            self.frame_set.mark_presented(frame.image_id);
            Ok(true)
        } else {
            println!("no prepared image to present");
            Ok(false)
        }
    }

    pub fn prepare_frame<F>(&mut self, f: F)
    where
        F: FnOnce(&mut Frame),
    {
        if let Some(image_id) = self.frame_set.get_available_image() {
            let mut frame =
                self.frames.get_mut(&image_id).expect("available frame to be in frame map");
            f(&mut frame);
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

    // this method is a hack that causes the application to terminate when
    // control-c is called and it has been run via scp/ssh.
    wait_for_close();

    let mut executor = fasync::Executor::new().context("Failed to create executor")?;

    executor.run_singlethreaded(async {
        // create async channel sender/receiver pair to receive vsync messages
        let (sender, mut receiver) = unbounded::<VSyncMessage>();

        // create a framebuffer
        let mut fb = FrameBuffer::new(None, Some(sender)).await?;

        // Find out the details of the display this frame buffer targets
        let config = fb.get_config();

        // Create our sample frame manager, which will create and manage the frames
        // and the display controller images they use.
        let frame_manager = Rc::new(RefCell::new(FrameManager::new(&mut fb).await?));

        // prepare the first frame
        frame_manager.borrow_mut().prepare_frame(|frame| {
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
        frame_manager.borrow_mut().prepare_frame(|frame| {
            update(&config, frame, 0).expect("update to work");
        });

        // keep track of the start time to use to animate the horizontal and
        // vertical lines.
        let start_time = Instant::now();

        // Create a clone of the Rc holding the frame manager to move into
        // the async block that receives messages about images being no longer
        // in use.
        let frame_manager_image = frame_manager.clone();

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
                    frame_manager.prepare_frame(|frame| {
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
