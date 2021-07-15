// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fuchsia_async::{self as fasync, DurationExt, Timer};
use fuchsia_framebuffer::{
    to_565, Config, Frame, FrameBuffer, FrameCollection, FrameCollectionBuilder, FrameSet,
    FrameUsage, ImageId, ImageInCollection, Message, PixelFormat,
};
use fuchsia_zircon::DurationNum;
use futures::{channel::mpsc::unbounded, future, StreamExt, TryFutureExt};
use std::{
    cell::RefCell,
    collections::BTreeSet,
    rc::Rc,
    time::{Duration, Instant},
};

struct FrameManager {
    frame_set: FrameSet,
    frame_collection: Option<FrameCollection>,
    owned: bool,
    collection_id: u64,
    start: Instant,
}

// this buffering approach will only work with two images, since
// as soon as it is released by the display controller it is prepared
// for use again and their can only be one prepared image.
const BUFFER_COUNT: usize = 2;

impl FrameManager {
    async fn make_frames(
        width: u32,
        height: u32,
        collection_id: u64,
        fb: &mut FrameBuffer,
    ) -> Result<(FrameCollection, FrameSet), Error> {
        let config = fb.get_config();
        let frame_collection_builder = FrameCollectionBuilder::new(
            width,
            height,
            config.format.into(),
            FrameUsage::Cpu,
            BUFFER_COUNT,
        )?;
        let config = fb.get_config();
        let mut frame_collection =
            frame_collection_builder.build(collection_id, &config, true, fb).await?;
        let mut available = BTreeSet::new();
        let config = fb.get_config();
        let image_ids = frame_collection.get_image_ids();
        for image_id in image_ids {
            let frame = frame_collection.get_frame_mut(image_id);
            let black = [0x00, 0x00, 0x00, 0xFF];
            if config.format != PixelFormat::Rgb565 {
                frame.fill_rectangle(0, 0, config.width, config.height, &black);
            } else {
                frame.fill_rectangle(0, 0, config.width, config.height, &to_565(&black));
            }
            available.insert(image_id);
        }
        Ok((frame_collection, FrameSet::new(collection_id, available)))
    }

    pub async fn new(fb: &mut FrameBuffer) -> Result<FrameManager, Error> {
        // this buffering approach will only work with two images, since
        // as soon as it is released by the display controller it is prepared
        // for use again and their can only be one prepared image.
        let collection_id = 10;
        let config = fb.get_config();
        let (frame_collection, frame_set) =
            Self::make_frames(config.width, config.height, collection_id, fb).await?;
        Ok(FrameManager {
            frame_set,
            frame_collection: Some(frame_collection),
            owned: true,
            collection_id,
            start: Instant::now(),
        })
    }

    pub fn present_prepared(
        &mut self,
        fb: &mut FrameBuffer,
        sender: Option<futures::channel::mpsc::UnboundedSender<ImageInCollection>>,
    ) -> Result<bool, Error> {
        if let Some(prepared) = self.frame_set.prepared {
            if self.owned {
                if let Some(frame_collection) = self.frame_collection.as_mut() {
                    let frame = frame_collection.get_frame(prepared);
                    frame.flush()?;
                    fb.present_frame(frame, sender, true)?;
                }
            }
            self.frame_set.mark_presented(prepared);
            Ok(true)
        } else {
            Ok(false)
        }
    }

    pub fn prepare_frame<F>(&mut self, _fb: &mut FrameBuffer, target: Instant, f: F)
    where
        F: FnOnce(Duration, &mut Frame),
    {
        if self.owned {
            if let Some(image_id) = self.frame_set.get_available_image() {
                if let Some(frame_collection) = self.frame_collection.as_mut() {
                    let frame = frame_collection.get_frame_mut(image_id);
                    let elapsed = target - self.start;
                    f(elapsed, frame);
                    self.frame_set.mark_prepared(image_id);
                }
            } else {
                println!("no free image to prepare");
            }
        }
    }

    pub fn frame_done_presenting(&mut self, image_id: ImageId) {
        self.frame_set.mark_done_presenting(image_id);
    }

    pub async fn ownership_changed(&mut self, owned: bool, fb: &mut FrameBuffer) {
        if owned != self.owned {
            self.owned = owned;
            if owned {
                self.collection_id += 1;
                let config = fb.get_config();
                let (frame_collection, frame_set) =
                    Self::make_frames(config.width, config.height, self.collection_id, fb)
                        .await
                        .expect("make_frames");
                self.frame_set = frame_set;
                self.frame_collection = Some(frame_collection);
            } else {
                let frame_collection = self.frame_collection.take();
                frame_collection.expect("frame_collection").release(fb);
            }
        }
    }
}

const FRAME_DELTA: u64 = 10_000_000;

fn update(config: &Config, frame: &mut Frame, duration: Duration) -> Result<(), Error> {
    let timestamp = duration.as_nanos() as u64;
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

    let mut executor = fasync::LocalExecutor::new().context("Failed to create executor")?;

    executor.run_singlethreaded(async {
        // create async channel sender/receiver pair to receive vsync messages
        let (sender, mut receiver) = unbounded::<Message>();

        // create a framebuffer
        let mut fb = FrameBuffer::new(FrameUsage::Cpu, None, None, Some(sender)).await?;

        // Find out the details of the display this frame buffer targets
        let config = fb.get_config();

        // Create our sample frame manager, which will create and manage the frames
        // and the display controller images they use.
        let frame_manager = Rc::new(RefCell::new(FrameManager::new(&mut fb).await?));

        fb.configure_layer(&config, 0).expect("configure_layer");

        // prepare the first frame
        frame_manager.borrow_mut().prepare_frame(&mut fb, Instant::now(), |duration, frame| {
            update(&config, frame, duration).expect("update to work");
        });

        // create an async channel sender/receiver pair to receive messages when
        // the image managed by a frame is no longer being displayed.
        let (image_sender, mut image_receiver) = unbounded::<ImageInCollection>();

        // Present the first image. Without this present, the display controller
        // will not send vsync messages.
        frame_manager
            .borrow_mut()
            .present_prepared(&mut fb, Some(image_sender.clone()))
            .expect("present to work");

        // Prepare a second image. There always wants to be a prepared image
        // to present at a fixed time after vsync.
        frame_manager.borrow_mut().prepare_frame(&mut fb, Instant::now(), |duration, frame| {
            update(&config, frame, duration).expect("update to work");
        });

        // Create a clone of the Rc holding the frame manager to move into
        // the async block that receives messages about images being no longer
        // in use.
        let frame_manager_image = frame_manager.clone();
        let frame_manager_ownership = frame_manager.clone();

        let fb_ptr = Rc::new(RefCell::new(fb));
        let fb_ptr2 = fb_ptr.clone();

        // wait for events from the image freed fence to know when an
        // image can prepared.
        fasync::Task::local(
            async move {
                while let Some(image_in_collection) = image_receiver.next().await {
                    // Grab a mutable reference. This is guaranteed to work
                    // since only one of this closure or the vsync closure can
                    // be in scope at once.
                    let mut frame_manager = frame_manager_image.borrow_mut();

                    // Note the freed image and immediately prepare it. Since there
                    // are only two frames we are guaranteed that at most one can
                    // be free at one time.
                    frame_manager.frame_done_presenting(image_in_collection.image_id);

                    // Use elapsed time to animate the horizontal and vertical
                    // lines
                    let mut fb = fb_ptr.borrow_mut();
                    frame_manager.prepare_frame(&mut fb, Instant::now(), |duration, frame| {
                        update(&config, frame, duration).expect("update to work");
                    });
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| {
                println!("error {:#?}", e);
            }),
        )
        .detach();

        // Listen for vsync messages to schedule an update of the displayed image
        fasync::Task::local(
            async move {
                while let Some(message) = receiver.next().await {
                    match message {
                        Message::Ownership(in_owned) => {
                            let mut frame_manager = frame_manager_ownership.borrow_mut();
                            frame_manager
                                .ownership_changed(in_owned, &mut fb_ptr2.borrow_mut())
                                .await;
                            if in_owned {
                                frame_manager.prepare_frame(
                                    &mut fb_ptr2.borrow_mut(),
                                    Instant::now(),
                                    |duration, frame| {
                                        update(&config, frame, duration).expect("update to work");
                                    },
                                );
                                frame_manager
                                    .present_prepared(
                                        &mut fb_ptr2.borrow_mut(),
                                        Some(image_sender.clone()),
                                    )
                                    .expect("present to work");
                                frame_manager.prepare_frame(
                                    &mut fb_ptr2.borrow_mut(),
                                    Instant::now(),
                                    |duration, frame| {
                                        update(&config, frame, duration).expect("update to work");
                                    },
                                );
                            }
                        }
                        Message::VSync(vsync_message) => {
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
                                .present_prepared(
                                    &mut fb_ptr2.borrow_mut(),
                                    Some(image_sender.clone()),
                                )
                                .expect("FrameManager::present_prepared to work");
                            fb_ptr2
                                .borrow_mut()
                                .acknowledge_vsync(vsync_message.cookie)
                                .unwrap_or_else(|e: anyhow::Error| {
                                    println!("acknowledge_vsync: error {:#?}", e);
                                });
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| {
                println!("error {:#?}", e);
            }),
        )
        .detach();
        Ok::<(), Error>(())
    })?;
    executor.run_singlethreaded(future::pending::<()>());
    unreachable!();
}
